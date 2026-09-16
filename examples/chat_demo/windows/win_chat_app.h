#ifndef APPTRAVERSE_EXAMPLE_CHAT_DEMO_WINDOWS_WIN_CHAT_APP_H_
#define APPTRAVERSE_EXAMPLE_CHAT_DEMO_WINDOWS_WIN_CHAT_APP_H_

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj_id.h"

#include "chat_launch_options.h"
#include "chat_model.h"
#include "chat_session.h"

namespace apptraverse::example::chat_demo {

class WinChatApp {
 public:
  WinChatApp();
  ~WinChatApp();

  int Run(ChatLaunchOptions options);

  // Accessible for smoke test harness
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
  ChatWorkspace::ptr const& ui_workspace() const { return ui_workspace_; }
  ChatSession& session() { return session_; }

 private:
  static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
  static LRESULT CALLBACK DraftEditSubclassProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
                                               UINT_PTR subclass_id, DWORD_PTR ref_data);

  LRESULT HandleMain(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

  void CreateControls(HWND hwnd);
  void LayoutControls(int width, int height);

  void OnInitialPublished();
  void OnIncrementalPublished();

  void UpdateUiFromWorkspace();
  void UpdateStatusLine();
  void UpdateTranscript();

  void SaveCurrentDraftAndScroll();
  void RestoreDraftAndScroll();

  void CaptureScrollAnchor(ScrollAnchor& anchor);
  void RestoreScrollAnchor(ScrollAnchor const& anchor);
  void RestoreWindowGeometry();

  void OnOpenPeerClicked();
  void OnSendDraftClicked();
  void OnChatSelectionChanged();
  void OnDraftChanged();
  void TryFinishClosing();

  HANDLE profile_lock_handle_{INVALID_HANDLE_VALUE};
  HMODULE richedit_module_{nullptr};

  ChatSession session_;
  HWND notify_hwnd_{nullptr};
  HWND main_hwnd_{nullptr};

  // Win32 controls
  HWND chat_list_hwnd_{nullptr};
  HWND transcript_hwnd_{nullptr};
  HWND draft_hwnd_{nullptr};
  HWND send_btn_hwnd_{nullptr};
  HWND admin_id_hwnd_{nullptr};
  HWND aether_uid_hwnd_{nullptr};
  HWND open_btn_hwnd_{nullptr};
  HWND status_label_hwnd_{nullptr};
  HWND presence_label_hwnd_{nullptr};

  // GUI-side Domain and Storage
  ae::RamDomainStorage ui_storage_;
  std::unique_ptr<ae::Domain> ui_domain_;
  ChatWorkspace::ptr ui_workspace_;

  // UI state tracking
  std::uint64_t local_edit_revision_{0};
  std::uint64_t last_published_edit_revision_{0};
  std::string active_chat_local_draft_;
  ae::ObjId active_entry_id_;

  bool applying_view_{false};
  bool closing_{false};
  bool geometry_restored_{false};
  std::chrono::steady_clock::time_point last_scroll_save_time_{};

  // Message position map for scroll restoration
  std::vector<std::pair<SharedEventId, LONG>> message_char_positions_;
};

}  // namespace apptraverse::example::chat_demo

#endif  // APPTRAVERSE_EXAMPLE_CHAT_DEMO_WINDOWS_WIN_CHAT_APP_H_
