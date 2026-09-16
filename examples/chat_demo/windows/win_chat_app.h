#ifndef APPTRAVERSE_EXAMPLE_CHAT_DEMO_WINDOWS_WIN_CHAT_APP_H_
#define APPTRAVERSE_EXAMPLE_CHAT_DEMO_WINDOWS_WIN_CHAT_APP_H_

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <map>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj_id.h"

#include "chat_connectivity.h"
#include "chat_launch_ipc.h"
#include "chat_launch_options.h"
#include "chat_model.h"
#include "chat_session.h"
#include "profile_lock.h"

namespace apptraverse::example::chat_demo {

struct WinChatGuiSnapshot {
  bool workspace_ready{false};
  int chat_count{0};
  ae::ObjId active_entry_id;
  int list_selection{-1};
  std::wstring draft;
  DWORD draft_sel_start{0};
  DWORD draft_sel_end{0};
  DesktopBounds bounds{};
  ScrollAnchor model_scroll{};
  ScrollAnchor measured_scroll{};
  bool maximized{false};
  std::wstring status_text;
};

class WinChatApp {
 public:
  WinChatApp();
  ~WinChatApp();

  int Run(ChatLaunchOptions options);

  HWND main_hwnd() const { return main_hwnd_; }
  HWND chat_list_hwnd() const { return chat_list_hwnd_; }
  HWND transcript_hwnd() const { return transcript_hwnd_; }
  HWND draft_hwnd() const { return draft_hwnd_; }
  HWND send_btn_hwnd() const { return send_btn_hwnd_; }
  HWND admin_id_hwnd() const { return admin_id_hwnd_; }
  HWND aether_uid_hwnd() const { return aether_uid_hwnd_; }
  HWND open_btn_hwnd() const { return open_btn_hwnd_; }
  HWND status_label_hwnd() const { return status_label_hwnd_; }
  HWND presence_label_hwnd() const { return presence_label_hwnd_; }

  void ApplyPublicationFromSession();
  ChatSession& session() { return session_; }

  WinChatGuiSnapshot BuildGuiSnapshot();
  bool TryQueryGuiSnapshot(WinChatGuiSnapshot& out);

 private:
  struct DraftEditState {
    std::uint64_t local_edit_revision{0};
    std::uint64_t last_published_edit_revision{0};
    std::string local_draft;
    DWORD sel_start{0};
    DWORD sel_end{0};
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

  static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
  static LRESULT CALLBACK IpcNotifyWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
  static LRESULT CALLBACK DraftEditSubclassProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
                                               UINT_PTR subclass_id, DWORD_PTR ref_data);

  LRESULT HandleMain(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

  void CreateControls(HWND hwnd);
  void LayoutControls(int width, int height);

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
  DesktopBounds CaptureBoundsLogical(HWND hwnd) const;
  void ApplyBoundsLogical(DesktopBounds const& bounds);

  int WindowLogicalDpi(HWND hwnd) const;

  void OnOpenPeerClicked();
  void OnSendDraftClicked();
  void OnChatSelectionChanged();
  void OnDraftChanged();
  void TryFinishClosing();
  void ShutdownSessionAndResources();
  void CreateIpcNotifyWindow(HINSTANCE hinst);
  void DestroyIpcNotifyWindow();
  LaunchIpcReply HandleLaunchIpcCopyData(COPYDATASTRUCT* cds);
  void ApplyPendingIpcOpenPeer();

  EntryViewState& ViewStateFor(ae::ObjId entry_id);
  ChatEntry::ptr FindUiEntry(ae::ObjId entry_id) const;

  bool IsImeComposing(HWND hwnd) const;
  bool IsNearBottom(HWND hwnd) const;
  LONG MessageCharPosition(SharedEventId const& message_id) const;
  void RebuildMessageCharPositions(ChatEntry::ptr const& entry);
  std::wstring FormatTranscriptLine(ChatEntry::ptr const& entry,
                                     MessageValue const& msg) const;
  bool CanAppendTranscript(ChatEntry::ptr const& entry) const;
  void AppendTranscriptLines(ChatEntry::ptr const& entry, std::size_t from_index);

  ProfileLock profile_lock_;
  std::string profile_key_;
  HMODULE richedit_module_{nullptr};

  ChatSession session_;
  HWND main_hwnd_{nullptr};
  HWND ipc_notify_hwnd_{nullptr};
  std::optional<OpenPeerRequest> pending_ipc_open_peer_;

  HWND chat_list_hwnd_{nullptr};
  HWND transcript_hwnd_{nullptr};
  HWND draft_hwnd_{nullptr};
  HWND send_btn_hwnd_{nullptr};
  HWND admin_id_hwnd_{nullptr};
  HWND aether_uid_hwnd_{nullptr};
  HWND open_btn_hwnd_{nullptr};
  HWND status_label_hwnd_{nullptr};
  HWND presence_label_hwnd_{nullptr};

  ae::RamDomainStorage ui_storage_;
  std::unique_ptr<ae::Domain> ui_domain_;
  ChatWorkspace::ptr ui_workspace_;

  std::map<ae::ObjId, EntryViewState> entry_views_;
  std::optional<ae::ObjId> pending_user_selection_;
  ae::ObjId active_entry_id_;
  ae::ObjId displayed_entry_id_;

  TranscriptCache transcript_cache_;
  std::vector<std::pair<SharedEventId, LONG>> message_char_positions_;

  bool applying_view_{false};
  bool closing_{false};
  bool geometry_restored_{false};
  bool class_registered_{false};
  std::optional<ScrollAnchor> pending_scroll_save_;
  std::chrono::steady_clock::time_point last_scroll_save_time_{};
  std::uint64_t pending_send_revision_{0};
  std::string local_send_error_;
};

}  // namespace apptraverse::example::chat_demo

#endif  // APPTRAVERSE_EXAMPLE_CHAT_DEMO_WINDOWS_WIN_CHAT_APP_H_
