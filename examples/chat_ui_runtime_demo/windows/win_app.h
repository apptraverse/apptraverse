#ifndef CHAT_WIN_APP_H_
#define CHAT_WIN_APP_H_

#include <filesystem>
#include <memory>

#include "apptraverse/runtime_lifecycle.h"
#include "apptraverse/ui_mirror.h"

#include "aether_runtime.h"
#include "chat_bootstrap.h"
#include "chat_model.h"
#include "ui_send_latency_tracker.h"
#include "win_presenters.h"

namespace chat::win32 {

class WinChatApp {
 public:
  int Run(std::filesystem::path const& state_dir, ChatCreateOptions create);

  void OnPublished(std::uint32_t root_id,
                   apptraverse::PublicationChannel<3>* channel);

 private:
  void ApplyPublication(std::uint32_t root_id,
                        apptraverse::PublicationChannel<3>* channel);
  void RequestExit();
  void HandleAetherUidOnModelThread(std::string uid_text);
  void HandleAetherFailedOnModelThread(std::string error);
  void HandlePresenceOnModelThread(PresenceState state);
  void HandleNetworkObservationOnModelThread(
      apptraverse::NetworkAvailability availability);

  ChatRuntime runtime_;
  std::unique_ptr<apptraverse::UiMirror> ui_mirror_;
  std::unique_ptr<apptraverse::ModelRuntime> model_runtime_;
  WinChatPresentationApplication::ptr presentation_;
  ChatAetherRuntime aether_runtime_;
  UiSendLatencyTracker latency_tracker_;
  HWND dispatcher_{nullptr};
  DWORD ui_thread_{0};
  bool exiting_{false};
  std::filesystem::path state_dir_;
};

}  // namespace chat::win32

#endif  // CHAT_WIN_APP_H_
