#include "chat_native_runtime.h"

#include <atomic>

#include "apptraverse/object_serialization.h"
#include "android_log.h"

namespace apptraverse::example::chat_demo::android {
namespace {

std::string EntryLabel(ChatEntry::ptr const& entry) {
  if (!entry.is_valid()) {
    return "";
  }
  return entry->display_name.empty() ? entry->peer_uid : entry->display_name;
}

}  // namespace

ChatNativeRuntime::ChatNativeRuntime(std::filesystem::path state_dir, ChatUiBridge ui_bridge,
                                     DemoRole role)
    : state_dir_{std::move(state_dir)}, ui_bridge_{std::move(ui_bridge)}, role_{role} {}

ChatNativeRuntime::~ChatNativeRuntime() {
  RequestStop();
  Join();
}

void ChatNativeRuntime::Start() {
  ChatSessionConfig cfg{.state_dir = state_dir_, .role = role_};
  session_.Start(std::move(cfg), [this]() { ui_bridge_.PostNotify(); });
  LogMarker("CHAT_NATIVE_STARTED");
}

void ChatNativeRuntime::Join() {
  session_.Join();
  stopped_.store(true, std::memory_order_release);
}

ChatNativeRuntime::EntryViewState& ChatNativeRuntime::ViewStateFor(ae::ObjId entry_id) {
  return entry_views_[entry_id];
}

ChatEntry::ptr ChatNativeRuntime::FindUiEntry(ae::ObjId entry_id) const {
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

std::string ChatNativeRuntime::FormatTranscriptLine(ChatEntry::ptr const& entry,
                                                    MessageValue const& msg) const {
  std::string author;
  if (ui_workspace_.is_valid() && !ui_workspace_->local_endpoint_uid.empty() &&
      msg.id.origin_uid == ui_workspace_->local_endpoint_uid) {
    author = "You";
  } else {
    author = EntryLabel(entry);
  }
  return "[" + author + "]: " + msg.text + "\n";
}

std::string ChatNativeRuntime::BuildTranscript(ChatEntry::ptr const& entry) const {
  if (!entry.is_valid() || !entry->room.is_valid()) {
    return "";
  }
  std::string out;
  for (auto const& msg : entry->room->messages) {
    out += FormatTranscriptLine(entry, msg);
  }
  return out;
}

std::string ChatNativeRuntime::BuildStatusLine() const {
  auto const status = session_.GetRuntimeStatus();
  if (!local_send_error_.empty()) {
    return "Error: " + local_send_error_;
  }
  if (!status.error_text.empty()) {
    return "Error: " + status.error_text;
  }
  if (status.lifecycle_state == SessionLifecycleState::kFailed) {
    return "Connection failed";
  }
  if (status.local_connectivity == LocalConnectivityState::kUnknown) {
    return "Starting...";
  }
  if (status.local_connectivity == LocalConnectivityState::kOffline) {
    return "Local Aether offline";
  }
  if (!status.local_endpoint_uid.empty()) {
    return "UID: " + status.local_endpoint_uid;
  }
  return "Starting...";
}

std::string ChatNativeRuntime::BuildPresenceLine() const {
  auto const status = session_.GetRuntimeStatus();
  auto const entry = FindUiEntry(active_entry_id_);
  if (!entry.is_valid() || !entry->peer_link.is_valid()) {
    return "";
  }
  std::string presence;
  std::string const& peer_uid = entry->peer_link->EndpointUid();
  auto const boot = status.room_bootstrap_by_peer_uid.find(peer_uid);
  if (boot != status.room_bootstrap_by_peer_uid.end() &&
      boot->second != RoomBootstrapState::kComplete) {
    return boot->second == RoomBootstrapState::kPending ? "Syncing..." : "Waiting room";
  }
  auto it = status.remote_presence.find(peer_uid);
  if (it != status.remote_presence.end()) {
    switch (it->second) {
      case PeerPresence::kOnline:
        presence = "Online";
        break;
      case PeerPresence::kConnecting:
        presence = "Connecting...";
        break;
      case PeerPresence::kOffline:
        presence = "Offline";
        break;
      default:
        presence = "Unknown";
        break;
    }
  }
  if (entry->room.is_valid() && !entry->room->messages.empty()) {
    SharedEventId const last_id = entry->room->messages.back().id;
    auto const delivery = status.delivery_by_event_id.find(last_id);
    if (delivery != status.delivery_by_event_id.end()) {
      switch (delivery->second) {
        case MessageDeliveryState::kDelivered:
          presence += " | Delivered";
          break;
        case MessageDeliveryState::kSending:
          presence += " | Sending";
          break;
        case MessageDeliveryState::kQueued:
          presence += " | Queued";
          break;
        default:
          break;
      }
    }
  }
  return presence;
}

void ChatNativeRuntime::ConsumeUiUpdatesLocked() {
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

  if (chat_switched && active_entry_id_.is_valid()) {
    auto entry = FindUiEntry(active_entry_id_);
    if (entry.is_valid()) {
      EntryViewState& view = ViewStateFor(entry.id());
      if (!view.live_scroll_valid) {
        view.live_scroll = entry->scroll;
        view.live_scroll_valid = true;
      }
    }
  }

  if (had_publication || ui_workspace_.is_valid()) {
    (void)chat_switched;
  }
}

void ChatNativeRuntime::PublishStateToJava() {
  std::vector<long> ids;
  std::vector<std::string> labels;
  if (ui_workspace_.is_valid()) {
    for (auto const& entry : ui_workspace_->chats) {
      if (!entry.is_valid()) {
        continue;
      }
      ids.push_back(static_cast<long>(entry.id().id()));
      labels.push_back(EntryLabel(entry));
      if (!active_entry_id_.is_valid()) {
        active_entry_id_ = entry.id();
      }
    }
  }

  std::string draft;
  ScrollAnchor scroll{};
  auto const entry = FindUiEntry(active_entry_id_);
  if (entry.is_valid()) {
    EntryViewState const& view = ViewStateFor(entry.id());
    draft = view.draft.local_draft.empty() ? entry->draft : view.draft.local_draft;
    scroll = view.live_scroll_valid ? view.live_scroll : entry->scroll;
  }

  auto const status = session_.GetRuntimeStatus();
  bool send_enabled =
      !status.local_endpoint_uid.empty() && entry.is_valid() && entry->room.is_valid();

  ui_bridge_.PostState(
      active_entry_id_.is_valid() ? static_cast<long>(active_entry_id_.id()) : 0L, ids, labels,
      draft, BuildTranscript(entry), BuildStatusLine(), BuildPresenceLine(), scroll.follow_tail,
      scroll.first_visible_message.origin_uid, scroll.first_visible_message.origin_sequence,
      scroll.offset_from_message_top, send_enabled, wide_layout_);
}

bool ChatNativeRuntime::ConsumeUiUpdate() {
  std::lock_guard<std::mutex> lock{mu_};
  ConsumeUiUpdatesLocked();
  PublishStateToJava();
  auto const status = session_.GetRuntimeStatus();
  if (status.lifecycle_state == SessionLifecycleState::kStopped ||
      status.lifecycle_state == SessionLifecycleState::kFailed) {
    stopped_.store(true, std::memory_order_release);
    ui_bridge_.PostStopped();
  }
  return ui_workspace_.is_valid();
}

void ChatNativeRuntime::JoinHost(std::string host_uid) {
  session_.SetHostUidInput(std::move(host_uid));
  session_.JoinHost();
}

void ChatNativeRuntime::SelectChat(ae::ObjId entry_id) {
  pending_user_selection_ = entry_id;
  active_entry_id_ = entry_id;
  session_.SelectChat(entry_id);
}

void ChatNativeRuntime::EditDraft(ae::ObjId entry_id, std::string text,
                                  std::uint64_t edit_revision) {
  EntryViewState& view = ViewStateFor(entry_id);
  view.draft.local_draft = std::move(text);
  view.draft.local_edit_revision = edit_revision;
  session_.EditDraft(entry_id, view.draft.local_draft, edit_revision);
}

void ChatNativeRuntime::SendDraft(ae::ObjId entry_id, std::string text,
                                  std::uint64_t edit_revision) {
  pending_send_revision_ = edit_revision;
  local_send_error_.clear();
  session_.SendDraft(entry_id, std::move(text), edit_revision);
}

void ChatNativeRuntime::SaveScroll(ae::ObjId entry_id, ScrollAnchor anchor) {
  EntryViewState& view = ViewStateFor(entry_id);
  view.live_scroll = anchor;
  view.live_scroll_valid = true;
  session_.SaveScroll(entry_id, anchor);
}

void ChatNativeRuntime::Checkpoint() {
  static std::atomic<std::uint64_t> next_checkpoint{1};
  std::uint64_t const id =
      next_checkpoint.fetch_add(1, std::memory_order_relaxed);
  if (!session_.Checkpoint(id)) {
    LogMarker("CHAT_ANDROID_CHECKPOINT_REJECTED");
    return;
  }
  // Completion is reported when completed_checkpoint_id advances in status.
  LogMarker("CHAT_ANDROID_CHECKPOINT_ENQUEUED");
}

void ChatNativeRuntime::RequestStop() { session_.RequestStop(); }

bool ChatNativeRuntime::IsStopped() const {
  return stopped_.load(std::memory_order_acquire);
}

}  // namespace apptraverse::example::chat_demo::android
