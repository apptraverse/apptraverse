#ifndef APPTRAVERSE_CHAT_WIN_APP_H_
#define APPTRAVERSE_CHAT_WIN_APP_H_

#include <chrono>
#include <memory>
#include <unordered_set>

#include "apptraverse/model_runtime.h"
#include "apptraverse/ui_mirror.h"

#include "aether_runtime.h"
#include "aether_shared_transport.h"
#include "chat_bootstrap.h"
#include "chat_connection_ui_state.h"
#include "chat_model.h"
#include "chat_shared.h"
#include "ui_send_latency_tracker.h"
#include "win_presenters.h"

namespace apptraverse {

class WinChatApp {
 public:
  int Run(std::filesystem::path const& state_dir, ChatRole role);

  void OnPublished(std::uint32_t root_id, PublicationChannel<3>* channel);

  // UI-thread handlers posted via the message-only dispatcher.
  void OnUiConnectionReady();
  void OnUiConnectionDisconnected();
  void OnUiRuntimeDiag();

 private:
  void ApplyPublication(std::uint32_t root_id, PublicationChannel<3>* channel);
  void RequestExit();
  void OnPeerReady(std::string remote_uid);
  void OnPeerClosed(std::string remote_uid);
  void OnPeerWriteFailed(std::string remote_uid);
  void OnPeerFrame(std::string remote_uid, std::vector<std::uint8_t> bytes);
  void OnPeerPresence(std::string remote_uid, bool online);
  void HandlePeerFrameOnModelThread(std::string remote_uid,
                                    std::vector<std::uint8_t> bytes);
  void TickDelivery();
  void MonitorRemoteOnce(std::string const& remote_uid);
  void PostConnectionUiReady();
  void PostConnectionUiDisconnected();
  void PostRuntimeDiagFromModelThread();

  ChatRuntime runtime_;
  std::unique_ptr<UiMirror> ui_mirror_;
  std::unique_ptr<ModelRuntime> model_runtime_;
  WinChatPresentationApplication::ptr presentation_;
  ChatAetherRuntime aether_runtime_;
  std::unique_ptr<AetherSharedTransport> shared_transport_;
  ChatSharedBinding shared_;
  UiSendLatencyTracker latency_tracker_;
  HWND dispatcher_{nullptr};
  DWORD ui_thread_{0};
  bool exiting_{false};
  std::unordered_set<std::string> monitored_remote_uids_;
  std::chrono::steady_clock::time_point last_delivery_tick_{};
  ChatRuntimeDiagUiState pending_diag_{};
  bool pending_diag_valid_{false};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_CHAT_WIN_APP_H_
