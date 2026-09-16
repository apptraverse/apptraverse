#ifndef APPTRAVERSE_EXAMPLE_CHAT_DEMO_LINUX_LINUX_CHAT_APP_H_
#define APPTRAVERSE_EXAMPLE_CHAT_DEMO_LINUX_LINUX_CHAT_APP_H_

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtk/gtk.h>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj_id.h"

#include "chat_connectivity.h"
#include "chat_launch_options.h"
#include "chat_model.h"
#include "chat_session.h"
#include "profile_lock.h"

namespace apptraverse::example::chat_demo {

struct LinuxChatGuiSnapshot {
  bool workspace_ready{false};
  int chat_count{0};
  ae::ObjId active_entry_id;
  int list_selection{-1};
  std::string draft;
  int draft_cursor_pos{0};
  DesktopBounds bounds{};
  ScrollAnchor model_scroll{};
  ScrollAnchor measured_scroll{};
  bool maximized{false};
  std::string status_text;
};

class LinuxChatApp {
 public:
  LinuxChatApp();
  ~LinuxChatApp();

  int Run(ChatLaunchOptions options);

  GtkWidget* main_window() const { return main_window_; }
  GtkWidget* chat_list() const { return chat_list_; }
  GtkWidget* transcript_view() const { return transcript_view_; }
  GtkWidget* draft_view() const { return draft_view_; }

  void ApplyPublicationFromSession();
  ChatSession& session() { return session_; }

  LinuxChatGuiSnapshot BuildGuiSnapshot();
  bool TryQueryGuiSnapshot(LinuxChatGuiSnapshot& out);

  void InvokeOnGui(std::function<void()> fn);

  // Smoke-test hook: same contract as Win32 WM_CHAT_TEST_SELECT.
  void TestSelectChatByIndex(int index);

 private:
  struct DraftEditState {
    std::uint64_t local_edit_revision{0};
    std::uint64_t last_published_edit_revision{0};
    std::string local_draft;
    int sel_start{0};
    int sel_end{0};
  };

  struct EntryViewState {
    DraftEditState draft;
    ScrollAnchor live_scroll{};
    bool live_scroll_valid{false};
  };

  struct TranscriptCache {
    ae::ObjId entry_id;
    std::size_t message_count{0};
    SharedEventId last_message_id{};
  };

  static gboolean OnDeleteEvent(GtkWidget* widget, GdkEvent* event, gpointer data);
  static void OnChatListRowActivated(GtkListBox* box, GtkListBoxRow* row, gpointer data);
  static void OnOpenClicked(GtkButton* button, gpointer data);
  static void OnSendClicked(GtkButton* button, gpointer data);
  static void OnDraftChanged(GtkTextBuffer* buffer, gpointer data);
  static void OnTranscriptVadjustmentChanged(GtkAdjustment* adj, gpointer data);
  static void OnConfigureEvent(GtkWidget* widget, GdkEventConfigure* event, gpointer data);
  static void OnWindowStateEvent(GtkWidget* widget, GdkEventWindowState* event,
                                 gpointer data);
  static gboolean OnGeometrySettleTimeout(gpointer data);
  static gboolean OnGuiInvokeIdle(gpointer data);

  void CreateControls();
  void LayoutControls();

  void ConsumeUiUpdates();
  void UpdateUiFromWorkspace(bool chat_switched);
  void UpdateStatusLine();
  void UpdateChatListSelection();
  void UpdateDraftFromModel(ChatEntry::ptr const& entry, bool chat_switched);
  void UpdateTranscript(ChatEntry::ptr const& entry, bool chat_switched,
                        ScrollAnchor const* restore_anchor);

  void SaveCurrentDraftAndScroll(bool flush_scroll);
  void RestoreDraftAndScroll(ae::ObjId entry_id);

  void CaptureScrollAnchor(ScrollAnchor& anchor);
  void RestoreScrollAnchor(ScrollAnchor const& anchor);
  void FlushPendingScrollSave();

  void RestoreWindowGeometry();
  void PersistWindowGeometry();
  DesktopBounds CaptureBoundsLogical() const;
  void ApplyBoundsLogical(DesktopBounds const& bounds);

  void OnJoinClicked();
  void OnSendDraftClicked();
  void OnChatSelectionChanged(ae::ObjId entry_id);
  void OnDraftChangedInternal();
  void TryFinishClosing();
  void ShutdownSessionAndResources();
  void RequestClose();

  EntryViewState& ViewStateFor(ae::ObjId entry_id);
  ChatEntry::ptr FindUiEntry(ae::ObjId entry_id) const;

  bool IsNearBottom() const;
  std::string FormatTranscriptLine(ChatEntry::ptr const& entry,
                                   MessageValue const& msg) const;
  bool CanAppendTranscript(ChatEntry::ptr const& entry) const;
  void AppendTranscriptLines(ChatEntry::ptr const& entry, std::size_t from_index);
  void RebuildMessageMarks(ChatEntry::ptr const& entry);
  void EnsureMessageMark(SharedEventId const& id, GtkTextIter const& iter);
  std::optional<SharedEventId> MessageAtTopOfView() const;

  static std::string GetEntryLabel(ChatEntry::ptr const& entry);
  static std::string GetTextBufferUtf8(GtkTextBuffer* buffer);
  static void SetTextBufferUtf8(GtkTextBuffer* buffer, std::string const& utf8);
  static void GetTextBufferSelection(GtkTextBuffer* buffer, int* start, int* end);

  ProfileLock profile_lock_;
  std::string profile_key_;

  ChatSession session_;
  GtkWidget* main_window_{nullptr};
  GtkWidget* chat_list_{nullptr};
  GtkWidget* transcript_view_{nullptr};
  GtkWidget* draft_view_{nullptr};
  GtkWidget* admin_id_entry_{nullptr};
  GtkWidget* aether_uid_entry_{nullptr};
  GtkWidget* open_btn_{nullptr};
  GtkWidget* send_btn_{nullptr};
  GtkWidget* status_label_{nullptr};
  GtkWidget* presence_label_{nullptr};
  GtkWidget* paned_{nullptr};

  ae::RamDomainStorage ui_storage_;
  std::unique_ptr<ae::Domain> ui_domain_;
  ChatWorkspace::ptr ui_workspace_;

  std::map<ae::ObjId, EntryViewState> entry_views_;
  std::optional<ae::ObjId> pending_user_selection_;
  ae::ObjId active_entry_id_;
  ae::ObjId displayed_entry_id_;

  TranscriptCache transcript_cache_;
  std::map<SharedEventId, GtkTextMark*> message_marks_;

  bool applying_view_{false};
  bool closing_{false};
  bool geometry_restored_{false};
  std::optional<ScrollAnchor> pending_scroll_save_;
  std::chrono::steady_clock::time_point last_scroll_save_time_{};
  guint geometry_settle_source_{0};
  std::uint64_t pending_send_revision_{0};
  std::string local_send_error_;
};

}  // namespace apptraverse::example::chat_demo

#endif  // APPTRAVERSE_EXAMPLE_CHAT_DEMO_LINUX_LINUX_CHAT_APP_H_
