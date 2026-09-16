#include "linux_chat_app.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <utility>

#include "apptraverse/object_serialization.h"
#include "chat_commands.h"
#include "chat_launch_ipc.h"
#include "linux_fatal.h"

namespace apptraverse::example::chat_demo {
namespace {

inline constexpr int kScrollCoalesceMs = 200;
inline constexpr int kGeometrySettleMs = 400;
inline constexpr int kBottomProximityPx = 8;

struct GuiInvokePayload {
  std::function<void()> fn;
};

std::filesystem::path DefaultStateDirectory() {
  char const* xdg = std::getenv("XDG_DATA_HOME");
  if (xdg != nullptr && xdg[0] != '\0') {
    return std::filesystem::path{xdg} / "AppTraverseChat";
  }
  char const* home = std::getenv("HOME");
  if (home != nullptr && home[0] != '\0') {
    return std::filesystem::path{home} / ".local" / "share" / "AppTraverseChat";
  }
  return std::filesystem::current_path() / "AppTraverseChat";
}

std::string MarkNameForMessage(SharedEventId const& id) {
  return "msg:" + id.origin_uid + ":" + std::to_string(id.origin_sequence);
}

}  // namespace

LinuxChatApp::LinuxChatApp() = default;

LinuxChatApp::~LinuxChatApp() {
  ShutdownSessionAndResources();
}

void LinuxChatApp::ShutdownSessionAndResources() {
  profile_lock_ = ProfileLock{};
  for (auto& [id, mark] : message_marks_) {
    if (mark != nullptr) {
      GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(transcript_view_));
      if (buffer != nullptr) {
        gtk_text_buffer_delete_mark(buffer, mark);
      }
    }
  }
  message_marks_.clear();
}

void LinuxChatApp::InvokeOnGui(std::function<void()> fn) {
  auto* payload = new GuiInvokePayload{std::move(fn)};
  g_idle_add(&LinuxChatApp::OnGuiInvokeIdle, payload);
}

gboolean LinuxChatApp::OnGuiInvokeIdle(gpointer data) {
  auto* payload = static_cast<GuiInvokePayload*>(data);
  payload->fn();
  delete payload;
  return G_SOURCE_REMOVE;
}

std::string LinuxChatApp::GetTextBufferUtf8(GtkTextBuffer* buffer) {
  GtkTextIter start;
  GtkTextIter end;
  gtk_text_buffer_get_bounds(buffer, &start, &end);
  char* text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
  std::string result = text != nullptr ? text : "";
  g_free(text);
  return result;
}

void LinuxChatApp::SetTextBufferUtf8(GtkTextBuffer* buffer, std::string const& utf8) {
  gtk_text_buffer_set_text(buffer, utf8.c_str(), static_cast<gint>(utf8.size()));
}

void LinuxChatApp::GetTextBufferSelection(GtkTextBuffer* buffer, int* start, int* end) {
  GtkTextIter s;
  GtkTextIter e;
  if (gtk_text_buffer_get_selection_bounds(buffer, &s, &e)) {
    *start = gtk_text_iter_get_offset(&s);
    *end = gtk_text_iter_get_offset(&e);
  } else {
    int const pos = gtk_text_iter_get_offset(&s);
    *start = pos;
    *end = pos;
  }
}

std::string LinuxChatApp::GetEntryLabel(ChatEntry::ptr const& entry) {
  if (!entry.is_valid()) {
    return "";
  }
  return entry->display_name.empty() ? entry->peer_admin_id : entry->display_name;
}

void LinuxChatApp::CreateControls() {
  main_window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(main_window_), "AppTraverse Chat");
  gtk_window_set_default_size(GTK_WINDOW(main_window_), 800, 600);
  g_signal_connect(main_window_, "delete-event", G_CALLBACK(OnDeleteEvent), this);
  g_signal_connect(main_window_, "configure-event", G_CALLBACK(OnConfigureEvent), this);
  g_signal_connect(main_window_, "window-state-event", G_CALLBACK(OnWindowStateEvent), this);

  GtkWidget* outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_container_add(GTK_CONTAINER(main_window_), outer);
  gtk_widget_set_margin_start(outer, 12);
  gtk_widget_set_margin_end(outer, 12);
  gtk_widget_set_margin_top(outer, 12);
  gtk_widget_set_margin_bottom(outer, 12);

  paned_ = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_box_pack_start(GTK_BOX(outer), paned_, TRUE, TRUE, 0);

  chat_list_ = gtk_list_box_new();
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(chat_list_), GTK_SELECTION_SINGLE);
  g_signal_connect(chat_list_, "row-activated", G_CALLBACK(OnChatListRowActivated), this);
  GtkWidget* list_scroll = gtk_scrolled_window_new(nullptr, nullptr);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(list_scroll), GTK_POLICY_NEVER,
                                 GTK_POLICY_AUTOMATIC);
  gtk_container_add(GTK_CONTAINER(list_scroll), chat_list_);
  gtk_paned_pack1(GTK_PANED(paned_), list_scroll, FALSE, FALSE);
  gtk_paned_set_position(GTK_PANED(paned_), 240);

  GtkWidget* right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_paned_pack2(GTK_PANED(paned_), right, TRUE, FALSE);

  GtkWidget* top_bar = gtk_grid_new();
  gtk_grid_set_column_spacing(GTK_GRID(top_bar), 6);
  gtk_box_pack_start(GTK_BOX(right), top_bar, FALSE, FALSE, 0);

  admin_id_entry_ = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(admin_id_entry_), "Admin ID");
  gtk_grid_attach(GTK_GRID(top_bar), admin_id_entry_, 0, 0, 1, 1);

  aether_uid_entry_ = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(aether_uid_entry_), "Aether UID (optional)");
  gtk_grid_attach(GTK_GRID(top_bar), aether_uid_entry_, 1, 0, 1, 1);

  open_btn_ = gtk_button_new_with_label("Open");
  g_signal_connect(open_btn_, "clicked", G_CALLBACK(OnOpenClicked), this);
  gtk_grid_attach(GTK_GRID(top_bar), open_btn_, 2, 0, 1, 1);

  transcript_view_ = gtk_text_view_new();
  gtk_text_view_set_editable(GTK_TEXT_VIEW(transcript_view_), FALSE);
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(transcript_view_), GTK_WRAP_WORD_CHAR);
  GtkWidget* transcript_scroll = gtk_scrolled_window_new(nullptr, nullptr);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(transcript_scroll), GTK_POLICY_NEVER,
                                 GTK_POLICY_AUTOMATIC);
  gtk_container_add(GTK_CONTAINER(transcript_scroll), transcript_view_);
  gtk_box_pack_start(GTK_BOX(right), transcript_scroll, TRUE, TRUE, 0);

  GtkAdjustment* vadj =
      gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(transcript_scroll));
  g_signal_connect(vadj, "value-changed", G_CALLBACK(OnTranscriptVadjustmentChanged), this);

  GtkWidget* draft_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  draft_view_ = gtk_text_view_new();
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(draft_view_), GTK_WRAP_WORD_CHAR);
  gtk_widget_set_size_request(draft_view_, -1, 90);
  GtkWidget* draft_scroll = gtk_scrolled_window_new(nullptr, nullptr);
  gtk_container_add(GTK_CONTAINER(draft_scroll), draft_view_);
  gtk_box_pack_start(GTK_BOX(draft_row), draft_scroll, TRUE, TRUE, 0);

  send_btn_ = gtk_button_new_with_label("Send");
  g_signal_connect(send_btn_, "clicked", G_CALLBACK(OnSendClicked), this);
  gtk_box_pack_start(GTK_BOX(draft_row), send_btn_, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(right), draft_row, FALSE, FALSE, 0);

  GtkTextBuffer* draft_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(draft_view_));
  g_signal_connect(draft_buffer, "changed", G_CALLBACK(OnDraftChanged), this);

  GtkWidget* status_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  status_label_ = gtk_label_new("Starting...");
  gtk_label_set_xalign(GTK_LABEL(status_label_), 0.0);
  gtk_box_pack_start(GTK_BOX(status_row), status_label_, TRUE, TRUE, 0);
  presence_label_ = gtk_label_new("");
  gtk_label_set_xalign(GTK_LABEL(presence_label_), 1.0);
  gtk_box_pack_start(GTK_BOX(status_row), presence_label_, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(outer), status_row, FALSE, FALSE, 0);
}

LinuxChatApp::EntryViewState& LinuxChatApp::ViewStateFor(ae::ObjId entry_id) {
  return entry_views_[entry_id];
}

ChatEntry::ptr LinuxChatApp::FindUiEntry(ae::ObjId entry_id) const {
  if (!ui_workspace_.is_valid() || !entry_id.is_valid()) {
    return {};
  }
  for (auto const& entry : ui_workspace_->chats) {
    if (entry.is_valid() && entry.id() == entry_id) {
      return entry;
    }
  }
  return {};
}

std::string LinuxChatApp::FormatTranscriptLine(ChatEntry::ptr const& entry,
                                               MessageValue const& msg) const {
  std::string author;
  if (ui_workspace_.is_valid() && !ui_workspace_->local_endpoint_uid.empty() &&
      msg.id.origin_uid == ui_workspace_->local_endpoint_uid) {
    author = "You";
  } else {
    author = GetEntryLabel(entry);
  }
  return "[" + author + "]: " + msg.text + "\n";
}

void LinuxChatApp::EnsureMessageMark(SharedEventId const& id, GtkTextIter const& iter) {
  GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(transcript_view_));
  std::string const name = MarkNameForMessage(id);
  GtkTextMark* mark = gtk_text_buffer_get_mark(buffer, name.c_str());
  if (mark == nullptr) {
    mark = gtk_text_buffer_create_mark(buffer, name.c_str(), &iter, TRUE);
    message_marks_[id] = mark;
  } else {
    gtk_text_buffer_move_mark(buffer, mark, &iter);
  }
}

void LinuxChatApp::RebuildMessageMarks(ChatEntry::ptr const& entry) {
  GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(transcript_view_));
  for (auto& [id, mark] : message_marks_) {
    if (mark != nullptr) {
      gtk_text_buffer_delete_mark(buffer, mark);
    }
  }
  message_marks_.clear();

  if (!entry.is_valid() || !entry->room.is_valid()) {
    return;
  }

  GtkTextIter iter;
  gtk_text_buffer_get_start_iter(buffer, &iter);
  for (auto const& msg : entry->room->messages) {
    EnsureMessageMark(msg.id, iter);
    std::string const line = FormatTranscriptLine(entry, msg);
    gtk_text_buffer_insert(buffer, &iter, line.c_str(), static_cast<gint>(line.size()));
  }
}

bool LinuxChatApp::CanAppendTranscript(ChatEntry::ptr const& entry) const {
  if (!entry.is_valid() || !entry->room.is_valid()) {
    return false;
  }
  if (transcript_cache_.entry_id != entry.id()) {
    return false;
  }
  if (transcript_cache_.message_count == 0 ||
      transcript_cache_.message_count >= entry->room->messages.size()) {
    return false;
  }
  auto const& prior = entry->room->messages[transcript_cache_.message_count - 1];
  return prior.id == transcript_cache_.last_message_id;
}

void LinuxChatApp::AppendTranscriptLines(ChatEntry::ptr const& entry, std::size_t from_index) {
  GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(transcript_view_));
  GtkTextIter iter;
  gtk_text_buffer_get_end_iter(buffer, &iter);

  for (std::size_t i = from_index; i < entry->room->messages.size(); ++i) {
    auto const& msg = entry->room->messages[i];
    EnsureMessageMark(msg.id, iter);
    std::string const line = FormatTranscriptLine(entry, msg);
    gtk_text_buffer_insert(buffer, &iter, line.c_str(), static_cast<gint>(line.size()));
    transcript_cache_.message_count = i + 1;
    transcript_cache_.last_message_id = msg.id;
  }
}

bool LinuxChatApp::IsNearBottom() const {
  GtkWidget* parent = gtk_widget_get_parent(transcript_view_);
  while (parent != nullptr && !GTK_IS_SCROLLED_WINDOW(parent)) {
    parent = gtk_widget_get_parent(parent);
  }
  if (parent == nullptr) {
    return true;
  }
  GtkAdjustment* adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(parent));
  if (adj == nullptr) {
    return true;
  }
  gdouble const upper = gtk_adjustment_get_upper(adj);
  gdouble const page = gtk_adjustment_get_page_size(adj);
  gdouble const value = gtk_adjustment_get_value(adj);
  return (upper - page - value) <= kBottomProximityPx;
}

std::optional<SharedEventId> LinuxChatApp::MessageAtTopOfView() const {
  GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(transcript_view_));
  GtkTextIter start;
  gtk_text_buffer_get_start_iter(buffer, &start);
  GtkTextIter top;
  if (!gtk_text_view_get_line_at_y(GTK_TEXT_VIEW(transcript_view_), &top, 0, nullptr)) {
    return std::nullopt;
  }
  for (auto const& [id, mark] : message_marks_) {
    if (mark == nullptr) {
      continue;
    }
    GtkTextIter mark_iter;
    gtk_text_buffer_get_iter_at_mark(buffer, &mark_iter, mark);
    if (gtk_text_iter_compare(&mark_iter, &top) <= 0) {
      return id;
    }
  }
  return std::nullopt;
}

void LinuxChatApp::CaptureScrollAnchor(ScrollAnchor& anchor) {
  anchor = ScrollAnchor{};
  if (IsNearBottom()) {
    anchor.follow_tail = true;
    return;
  }
  auto const visible = MessageAtTopOfView();
  if (!visible.has_value()) {
    anchor.follow_tail = true;
    return;
  }
  GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(transcript_view_));
  std::string const name = MarkNameForMessage(*visible);
  GtkTextMark* mark = gtk_text_buffer_get_mark(buffer, name.c_str());
  if (mark == nullptr) {
    anchor.follow_tail = true;
    return;
  }
  GtkTextIter mark_iter;
  gtk_text_buffer_get_iter_at_mark(buffer, &mark_iter, mark);
  GdkRectangle rect;
  gtk_text_view_get_line_yrange(GTK_TEXT_VIEW(transcript_view_), &mark_iter, &rect.y, &rect.height);
  anchor.follow_tail = false;
  anchor.first_visible_message = *visible;
  anchor.offset_from_message_top = static_cast<double>(rect.y);
}

void LinuxChatApp::RestoreScrollAnchor(ScrollAnchor const& anchor) {
  GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(transcript_view_));
  if (anchor.follow_tail) {
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(transcript_view_), &end, 0.0, FALSE, 0.0, 1.0);
    return;
  }
  std::string const name = MarkNameForMessage(anchor.first_visible_message);
  GtkTextMark* mark = gtk_text_buffer_get_mark(buffer, name.c_str());
  if (mark == nullptr) {
    GtkTextIter start;
    gtk_text_buffer_get_start_iter(buffer, &start);
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(transcript_view_), &start, 0.0, FALSE, 0.0, 0.0);
    return;
  }
  GtkTextIter mark_iter;
  gtk_text_buffer_get_iter_at_mark(buffer, &mark_iter, mark);
  gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(transcript_view_), mark, 0.0, FALSE, 0.0, 0.0);
  if (anchor.offset_from_message_top != 0.0) {
    GtkWidget* parent = gtk_widget_get_parent(transcript_view_);
    while (parent != nullptr && !GTK_IS_SCROLLED_WINDOW(parent)) {
      parent = gtk_widget_get_parent(parent);
    }
    if (parent != nullptr) {
      GtkAdjustment* adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(parent));
      if (adj != nullptr) {
        gdouble const value = gtk_adjustment_get_value(adj);
        gtk_adjustment_set_value(adj, value + anchor.offset_from_message_top);
      }
    }
  }
}

void LinuxChatApp::OnDraftChangedInternal() {
  if (applying_view_ || !active_entry_id_.is_valid()) {
    return;
  }
  GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(draft_view_));
  std::string const text = GetTextBufferUtf8(buffer);
  EntryViewState& view = ViewStateFor(active_entry_id_);
  view.draft.local_draft = text;
  GetTextBufferSelection(buffer, &view.draft.sel_start, &view.draft.sel_end);
  ++view.draft.local_edit_revision;
  session_.EditDraft(active_entry_id_, text, view.draft.local_edit_revision);
}

void LinuxChatApp::OnSendDraftClicked() {
  if (!active_entry_id_.is_valid()) {
    return;
  }
  GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(draft_view_));
  std::string const current_text = GetTextBufferUtf8(buffer);
  if (current_text.empty()) {
    return;
  }
  EntryViewState& view = ViewStateFor(active_entry_id_);
  ++view.draft.local_edit_revision;
  pending_send_revision_ = view.draft.local_edit_revision;
  local_send_error_.clear();
  session_.SendDraft(active_entry_id_, current_text, view.draft.local_edit_revision);
}

void LinuxChatApp::OnOpenPeerClicked() {
  std::string const admin_id = gtk_entry_get_text(GTK_ENTRY(admin_id_entry_));
  std::string const peer_uid = gtk_entry_get_text(GTK_ENTRY(aether_uid_entry_));
  if (admin_id.empty()) {
    return;
  }
  OpenPeerRequest req{
      .peer_admin_id = admin_id,
      .peer_aether_uid = peer_uid.empty() ? std::nullopt : std::optional<std::string>(peer_uid),
  };
  session_.OpenPeer(std::move(req));
}

void LinuxChatApp::FlushPendingScrollSave() {
  if (!pending_scroll_save_.has_value() || !active_entry_id_.is_valid()) {
    return;
  }
  session_.SaveScroll(active_entry_id_, *pending_scroll_save_);
  EntryViewState& view = ViewStateFor(active_entry_id_);
  view.live_scroll = *pending_scroll_save_;
  view.live_scroll_valid = true;
  pending_scroll_save_.reset();
}

void LinuxChatApp::SaveCurrentDraftAndScroll(bool flush_scroll) {
  if (!active_entry_id_.is_valid()) {
    return;
  }
  GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(draft_view_));
  EntryViewState& view = ViewStateFor(active_entry_id_);
  view.draft.local_draft = GetTextBufferUtf8(buffer);
  GetTextBufferSelection(buffer, &view.draft.sel_start, &view.draft.sel_end);
  if (flush_scroll) {
    FlushPendingScrollSave();
  }
  ScrollAnchor anchor{};
  CaptureScrollAnchor(anchor);
  session_.SaveScroll(active_entry_id_, anchor);
  view.live_scroll = anchor;
  view.live_scroll_valid = true;
}

void LinuxChatApp::UpdateDraftFromModel(ChatEntry::ptr const& entry, bool chat_switched) {
  if (!entry.is_valid()) {
    return;
  }
  EntryViewState& view = ViewStateFor(entry.id());
  bool const may_replace =
      chat_switched || view.draft.local_edit_revision <= view.draft.last_published_edit_revision;
  if (!may_replace) {
    return;
  }

  std::string display_draft = entry->draft;
  if (chat_switched && !view.draft.local_draft.empty()) {
    display_draft = view.draft.local_draft;
  } else if (view.draft.local_draft != entry->draft) {
    display_draft = entry->draft;
  } else {
    display_draft = view.draft.local_draft;
  }

  if (view.draft.local_draft != display_draft) {
    view.draft.local_draft = display_draft;
  }

  GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(draft_view_));
  if (GetTextBufferUtf8(buffer) != display_draft) {
    applying_view_ = true;
    SetTextBufferUtf8(buffer, display_draft);
    applying_view_ = false;
  }
  if (chat_switched) {
    GtkTextIter start;
    GtkTextIter end;
    gtk_text_buffer_get_start_iter(buffer, &start);
    gtk_text_iter_set_offset(&start, view.draft.sel_start);
    gtk_text_iter_set_offset(&end, view.draft.sel_end);
    gtk_text_buffer_select_range(buffer, &start, &end);
  }
}

void LinuxChatApp::UpdateTranscript(ChatEntry::ptr const& entry, bool chat_switched,
                                    ScrollAnchor const* restore_anchor) {
  ScrollAnchor captured{};
  bool const preserve_live = !chat_switched && restore_anchor == nullptr;
  if (preserve_live) {
    CaptureScrollAnchor(captured);
  }

  if (!entry.is_valid() || !entry->room.is_valid()) {
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(transcript_view_));
    gtk_text_buffer_set_text(buffer, "", 0);
    transcript_cache_ = {};
    return;
  }

  bool const did_append = !chat_switched && CanAppendTranscript(entry);
  if (chat_switched || !did_append) {
    RebuildMessageMarks(entry);
    transcript_cache_.entry_id = entry.id();
    transcript_cache_.message_count = entry->room->messages.size();
    if (!entry->room->messages.empty()) {
      transcript_cache_.last_message_id = entry->room->messages.back().id;
    }
  } else {
    AppendTranscriptLines(entry, transcript_cache_.message_count);
  }

  displayed_entry_id_ = entry.id();

  if (restore_anchor != nullptr) {
    RestoreScrollAnchor(*restore_anchor);
  } else if (preserve_live) {
    RestoreScrollAnchor(captured.follow_tail ? ScrollAnchor{.follow_tail = true} : captured);
  }
}

void LinuxChatApp::UpdateChatListSelection() {
  if (!ui_workspace_.is_valid()) {
    return;
  }

  ae::ObjId target = active_entry_id_;
  if (pending_user_selection_.has_value()) {
    target = *pending_user_selection_;
  } else if (ui_workspace_->selected_chat_id.is_valid()) {
    target = ui_workspace_->selected_chat_id;
  }

  GList* children = gtk_container_get_children(GTK_CONTAINER(chat_list_));
  for (GList* node = children; node != nullptr; node = node->next) {
    gtk_widget_destroy(GTK_WIDGET(node->data));
  }
  g_list_free(children);

  int list_index = 0;
  GtkListBoxRow* selected_row = nullptr;
  for (auto const& entry : ui_workspace_->chats) {
    if (!entry.is_valid()) {
      continue;
    }
    GtkWidget* row = gtk_list_box_row_new();
    GtkWidget* label = gtk_label_new(GetEntryLabel(entry).c_str());
    gtk_widget_set_margin_start(label, 6);
    gtk_widget_set_margin_end(label, 6);
    gtk_widget_set_margin_top(label, 4);
    gtk_widget_set_margin_bottom(label, 4);
    gtk_container_add(GTK_CONTAINER(row), label);
    g_object_set_data(G_OBJECT(row), "entry-id",
                      GUINT_TO_POINTER(static_cast<guint>(entry.id().id())));
    gtk_list_box_insert(GTK_LIST_BOX(chat_list_), row, -1);

    if (entry.id() == target) {
      selected_row = GTK_LIST_BOX_ROW(row);
    }
    if (!active_entry_id_.is_valid() && list_index == 0) {
      active_entry_id_ = entry.id();
      selected_row = GTK_LIST_BOX_ROW(row);
    }
    ++list_index;
  }

  gtk_widget_show_all(chat_list_);
  if (selected_row != nullptr) {
    gtk_list_box_select_row(GTK_LIST_BOX(chat_list_), selected_row);
  }
}

void LinuxChatApp::UpdateStatusLine() {
  auto const status = session_.GetRuntimeStatus();
  std::string status_text;
  if (!local_send_error_.empty()) {
    status_text = "Error: " + local_send_error_;
  } else if (!status.error_text.empty()) {
    status_text = "Error: " + status.error_text;
  } else if (status.lifecycle_state == SessionLifecycleState::kFailed) {
    status_text = "Connection failed";
  } else if (status.local_connectivity == LocalConnectivityState::kUnknown) {
    status_text = "Starting...";
  } else if (status.local_connectivity == LocalConnectivityState::kOffline) {
    status_text = "Local Aether offline";
  } else if (!status.local_endpoint_uid.empty()) {
    status_text = "UID: " + status.local_endpoint_uid;
  } else {
    status_text = "Starting...";
  }
  gtk_label_set_text(GTK_LABEL(status_label_), status_text.c_str());

  std::string presence_text;
  auto const entry = FindUiEntry(active_entry_id_);
  if (entry.is_valid() && entry->peer_link.is_valid()) {
    std::string const& peer_uid = entry->peer_link->EndpointUid();
    auto const boot = status.room_bootstrap_by_peer_uid.find(peer_uid);
    if (boot != status.room_bootstrap_by_peer_uid.end() &&
        boot->second != RoomBootstrapState::kComplete) {
      presence_text = boot->second == RoomBootstrapState::kPending ? "Syncing..." : "Waiting room";
    } else {
      auto it = status.remote_presence.find(peer_uid);
      if (it != status.remote_presence.end()) {
        switch (it->second) {
          case PeerPresence::kOnline:
            presence_text = "Online";
            break;
          case PeerPresence::kConnecting:
            presence_text = "Connecting...";
            break;
          case PeerPresence::kOffline:
            presence_text = "Offline";
            break;
          default:
            presence_text = "Unknown";
            break;
        }
      }
    }
    if (entry->room.is_valid() && !entry->room->messages.empty()) {
      SharedEventId const last_id = entry->room->messages.back().id;
      auto const delivery = status.delivery_by_event_id.find(last_id);
      if (delivery != status.delivery_by_event_id.end()) {
        switch (delivery->second) {
          case MessageDeliveryState::kDelivered:
            presence_text += " | Delivered";
            break;
          case MessageDeliveryState::kSending:
            presence_text += " | Sending";
            break;
          case MessageDeliveryState::kQueued:
            presence_text += " | Queued";
            break;
          default:
            break;
        }
      }
    }
  }
  gtk_label_set_text(GTK_LABEL(presence_label_), presence_text.c_str());

  bool send_enabled = false;
  if (!status.local_endpoint_uid.empty() && entry.is_valid() && entry->room.is_valid()) {
    send_enabled = true;
  }
  gtk_widget_set_sensitive(send_btn_, send_enabled ? TRUE : FALSE);
}

void LinuxChatApp::UpdateUiFromWorkspace(bool chat_switched) {
  if (!ui_workspace_.is_valid()) {
    return;
  }

  applying_view_ = true;
  UpdateChatListSelection();

  bool const selection_changed =
      chat_switched || (displayed_entry_id_.is_valid() && displayed_entry_id_ != active_entry_id_);
  if (selection_changed) {
    displayed_entry_id_ = active_entry_id_;
  }

  auto const entry = FindUiEntry(active_entry_id_);
  if (entry.is_valid()) {
    UpdateDraftFromModel(entry, selection_changed);
    ScrollAnchor const* restore = nullptr;
    if (selection_changed) {
      EntryViewState const& view = ViewStateFor(entry.id());
      restore = view.live_scroll_valid ? &view.live_scroll : &entry->scroll;
    }
    UpdateTranscript(entry, selection_changed, restore);
  } else {
    UpdateTranscript({}, selection_changed, nullptr);
  }

  UpdateStatusLine();
  applying_view_ = false;
}

void LinuxChatApp::ConsumeUiUpdates() {
  bool chat_switched = false;
  bool had_publication = false;

  while (auto update = session_.TryTakeUiUpdate()) {
    if (update->publication_bytes.has_value() && !update->publication_bytes->empty()) {
      had_publication = true;
      auto const& bytes = *update->publication_bytes;
      ByteSource in{bytes.data(), bytes.size()};
      if (!ui_workspace_.is_valid()) {
        ui_domain_ = std::make_unique<ae::Domain>(ui_storage_);
        auto loaded = LoadInitialPublication(in, *ui_domain_, ui_storage_);
        auto held = ui_domain_->Find(loaded->obj_id);
        ui_workspace_ =
            ChatWorkspace::ptr{ui_domain_.get(), loaded->obj_id, {}, std::move(held)};
        RestoreWindowGeometry();
        if (!active_entry_id_.is_valid() && ui_workspace_->selected_chat_id.is_valid()) {
          active_entry_id_ = ui_workspace_->selected_chat_id;
        }
      } else {
        ae::ObjId const prior_selected = active_entry_id_;
        ApplyStructuralPublicationAndUpdatePresenters(
            in, *ui_domain_, ui_storage_, *ui_workspace_);
        if (!pending_user_selection_.has_value() && ui_workspace_->selected_chat_id.is_valid() &&
            ui_workspace_->selected_chat_id != prior_selected) {
          chat_switched = true;
          active_entry_id_ = ui_workspace_->selected_chat_id;
        }
      }
    }

    if (update->selected_chat_ack.has_value()) {
      if (pending_user_selection_ == *update->selected_chat_ack) {
        pending_user_selection_.reset();
      }
    }

    for (auto const& [entry_id, rev] : update->processed_edit_revisions_by_entry) {
      EntryViewState& view = ViewStateFor(entry_id);
      view.draft.last_published_edit_revision = rev;
      if (entry_id == active_entry_id_ && pending_send_revision_ == rev) {
        pending_send_revision_ = 0;
        local_send_error_.clear();
      }
    }
  }

  if (pending_send_revision_ != 0) {
    local_send_error_ = "Message not sent";
  }

  if (had_publication || ui_workspace_.is_valid()) {
    UpdateUiFromWorkspace(chat_switched);
  } else {
    UpdateStatusLine();
  }
}

void LinuxChatApp::TryFinishClosing() {
  if (!closing_) {
    return;
  }
  if (!session_.IsFinished()) {
    return;
  }
  session_.Join();
  gtk_widget_destroy(main_window_);
  main_window_ = nullptr;
  gtk_main_quit();
}

void LinuxChatApp::ApplyPublicationFromSession() {
  ConsumeUiUpdates();
  TryFinishClosing();
}

DesktopBounds LinuxChatApp::CaptureBoundsLogical() const {
  DesktopBounds bounds{};
  if (main_window_ == nullptr) {
    return bounds;
  }
  gint x = 0;
  gint y = 0;
  gint w = 0;
  gint h = 0;
  gtk_window_get_position(GTK_WINDOW(main_window_), &x, &y);
  gtk_window_get_size(GTK_WINDOW(main_window_), &w, &h);
  GdkWindowState state = gdk_window_get_state(gtk_widget_get_window(main_window_));
  bounds.valid = true;
  bounds.x = x;
  bounds.y = y;
  bounds.width = w;
  bounds.height = h;
  bounds.maximized = (state & GDK_WINDOW_STATE_MAXIMIZED) != 0;
  return bounds;
}

void LinuxChatApp::ApplyBoundsLogical(DesktopBounds const& bounds) {
  if (!bounds.valid || bounds.width <= 0 || bounds.height <= 0 || main_window_ == nullptr) {
    return;
  }
  gtk_window_resize(GTK_WINDOW(main_window_), bounds.width, bounds.height);
  gtk_window_move(GTK_WINDOW(main_window_), bounds.x, bounds.y);
  if (bounds.maximized) {
    gtk_window_maximize(GTK_WINDOW(main_window_));
  }
}

void LinuxChatApp::RestoreWindowGeometry() {
  if (geometry_restored_ || !ui_workspace_.is_valid()) {
    return;
  }
  geometry_restored_ = true;
  ApplyBoundsLogical(ui_workspace_->desktop_bounds);
}

void LinuxChatApp::PersistWindowGeometry() {
  if (main_window_ == nullptr || closing_) {
    return;
  }
  DesktopBounds const bounds = CaptureBoundsLogical();
  if (bounds.width > 0 && bounds.height > 0) {
    session_.SaveBounds(bounds);
  }
}

void LinuxChatApp::RestoreDraftAndScroll(ae::ObjId entry_id) {
  if (!entry_id.is_valid()) {
    return;
  }
  ChatEntry::ptr entry = FindUiEntry(entry_id);
  if (!entry.is_valid()) {
    return;
  }
  applying_view_ = true;
  displayed_entry_id_ = entry_id;
  UpdateDraftFromModel(entry, true);
  UpdateTranscript(entry, true, &entry->scroll);
  applying_view_ = false;
}

void LinuxChatApp::OnChatSelectionChanged(ae::ObjId entry_id) {
  if (!entry_id.is_valid() || entry_id == active_entry_id_) {
    return;
  }
  SaveCurrentDraftAndScroll(true);
  active_entry_id_ = entry_id;
  pending_user_selection_ = entry_id;
  session_.SelectChat(active_entry_id_);
  RestoreDraftAndScroll(entry_id);
}

LinuxChatGuiSnapshot LinuxChatApp::BuildGuiSnapshot() {
  ApplyPublicationFromSession();
  LinuxChatGuiSnapshot snap{};
  snap.workspace_ready = ui_workspace_.is_valid();
  snap.active_entry_id = active_entry_id_;
  if (ui_workspace_.is_valid()) {
    snap.chat_count = static_cast<int>(ui_workspace_->chats.size());
    snap.bounds = ui_workspace_->desktop_bounds;
    auto const entry = FindUiEntry(active_entry_id_);
    if (entry.is_valid()) {
      snap.model_scroll = entry->scroll;
    }
  }
  GtkListBoxRow* row = gtk_list_box_get_selected_row(GTK_LIST_BOX(chat_list_));
  if (row != nullptr) {
    snap.list_selection = gtk_list_box_row_get_index(row);
  }
  snap.draft = GetTextBufferUtf8(gtk_text_view_get_buffer(GTK_TEXT_VIEW(draft_view_)));
  GetTextBufferSelection(gtk_text_view_get_buffer(GTK_TEXT_VIEW(draft_view_)),
                         &snap.draft_cursor_pos, &snap.draft_cursor_pos);
  snap.bounds = CaptureBoundsLogical();
  snap.maximized = snap.bounds.maximized;
  CaptureScrollAnchor(snap.measured_scroll);
  snap.status_text = gtk_label_get_text(GTK_LABEL(status_label_));
  return snap;
}

bool LinuxChatApp::TryQueryGuiSnapshot(LinuxChatGuiSnapshot& out) {
  if (main_window_ == nullptr) {
    return false;
  }
  out = BuildGuiSnapshot();
  return true;
}

void LinuxChatApp::TestSelectChatByIndex(int index) {
  GtkListBoxRow* row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(chat_list_), index);
  if (row == nullptr) {
    return;
  }
  gtk_list_box_select_row(GTK_LIST_BOX(chat_list_), row);
  ae::ObjId const entry_id{static_cast<std::uint32_t>(
      GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(row), "entry-id")))};
  OnChatSelectionChanged(entry_id);
  ApplyPublicationFromSession();
}

void LinuxChatApp::RequestClose() {
  if (closing_) {
    return;
  }
  SaveCurrentDraftAndScroll(true);
  PersistWindowGeometry();
  closing_ = true;
  gtk_widget_set_sensitive(main_window_, FALSE);
  session_.RequestStop();
  TryFinishClosing();
}

gboolean LinuxChatApp::OnDeleteEvent(GtkWidget*, GdkEvent*, gpointer data) {
  auto* app = static_cast<LinuxChatApp*>(data);
  app->RequestClose();
  return TRUE;
}

void LinuxChatApp::OnChatListRowActivated(GtkListBox*, GtkListBoxRow* row, gpointer data) {
  auto* app = static_cast<LinuxChatApp*>(data);
  ae::ObjId const entry_id{static_cast<std::uint32_t>(
      GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(row), "entry-id")))};
  app->OnChatSelectionChanged(entry_id);
}

void LinuxChatApp::OnOpenClicked(GtkButton*, gpointer data) {
  static_cast<LinuxChatApp*>(data)->OnOpenPeerClicked();
}

void LinuxChatApp::OnSendClicked(GtkButton*, gpointer data) {
  static_cast<LinuxChatApp*>(data)->OnSendDraftClicked();
}

void LinuxChatApp::OnDraftChanged(GtkTextBuffer*, gpointer data) {
  static_cast<LinuxChatApp*>(data)->OnDraftChangedInternal();
}

void LinuxChatApp::OnTranscriptVadjustmentChanged(GtkAdjustment*, gpointer data) {
  auto* app = static_cast<LinuxChatApp*>(data);
  if (app->applying_view_ || !app->active_entry_id_.is_valid()) {
    return;
  }
  ScrollAnchor anchor{};
  app->CaptureScrollAnchor(anchor);
  app->pending_scroll_save_ = anchor;
  EntryViewState& view = app->ViewStateFor(app->active_entry_id_);
  view.live_scroll = anchor;
  view.live_scroll_valid = true;
  auto const now = std::chrono::steady_clock::now();
  if (now - app->last_scroll_save_time_ >= std::chrono::milliseconds(kScrollCoalesceMs)) {
    app->last_scroll_save_time_ = now;
    app->FlushPendingScrollSave();
  }
}

gboolean LinuxChatApp::OnGeometrySettleTimeout(gpointer data) {
  auto* app = static_cast<LinuxChatApp*>(data);
  app->geometry_settle_source_ = 0;
  app->PersistWindowGeometry();
  return G_SOURCE_REMOVE;
}

void LinuxChatApp::OnConfigureEvent(GtkWidget*, GdkEventConfigure*, gpointer data) {
  auto* app = static_cast<LinuxChatApp*>(data);
  if (app->geometry_settle_source_ != 0) {
    g_source_remove(app->geometry_settle_source_);
  }
  app->geometry_settle_source_ =
      g_timeout_add(kGeometrySettleMs, OnGeometrySettleTimeout, app);
}

void LinuxChatApp::OnWindowStateEvent(GtkWidget*, GdkEventWindowState*, gpointer data) {
  auto* app = static_cast<LinuxChatApp*>(data);
  if (app->geometry_settle_source_ != 0) {
    g_source_remove(app->geometry_settle_source_);
  }
  app->geometry_settle_source_ =
      g_timeout_add(kGeometrySettleMs, OnGeometrySettleTimeout, app);
}

int LinuxChatApp::Run(ChatLaunchOptions options) {
  gtk_init(nullptr, nullptr);

  std::filesystem::path state_dir =
      options.state_dir.has_value() ? std::filesystem::path{*options.state_dir}
                                    : DefaultStateDirectory();
  std::filesystem::create_directories(state_dir);
  profile_key_ = NormalizeProfileKey(state_dir);

  ProfileLock candidate;
  ProfileLock::AcquireResult const lock_result = ProfileLock::TryAcquire(state_dir, candidate);
  if (lock_result == ProfileLock::AcquireResult::kBusy) {
    GtkWidget* dialog = gtk_message_dialog_new(
        nullptr, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
        "Profile already open in another process.");
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    return 1;
  }
  if (lock_result == ProfileLock::AcquireResult::kError) {
    GtkWidget* dialog = gtk_message_dialog_new(
        nullptr, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
        "Could not acquire profile lock.");
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    return 1;
  }
  profile_lock_ = std::move(candidate);

  CreateControls();
  gtk_widget_show_all(main_window_);

  ChatSessionConfig cfg{
      .state_dir = state_dir,
      .initial_open_peer = options.open_peer,
  };

  session_.Start(std::move(cfg), [this]() {
    InvokeOnGui([this]() {
      ApplyPublicationFromSession();
    });
  });

  gtk_main();

  if (!closing_) {
    session_.RequestStop();
  }
  session_.Join();
  ShutdownSessionAndResources();
  return 0;
}

}  // namespace apptraverse::example::chat_demo
