#ifndef APPTRAVERSE_CHAT_WIN_APP_H_
#define APPTRAVERSE_CHAT_WIN_APP_H_

#include <memory>

#include "apptraverse/model_runtime.h"
#include "apptraverse/ui_mirror.h"

#include "aether_runtime.h"
#include "chat_bootstrap.h"
#include "chat_shared.h"
#include "win_presenters.h"

namespace apptraverse {

class WinChatApp {
 public:
  int Run(std::filesystem::path const& state_dir, ChatRole role);

  void OnPublished(std::uint32_t root_id, PublicationChannel<3>* channel);

 private:
  void ApplyPublication(std::uint32_t root_id, PublicationChannel<3>* channel);
  void RequestExit();

  ChatRuntime runtime_;
  std::unique_ptr<UiMirror> ui_mirror_;
  std::unique_ptr<ModelRuntime> model_runtime_;
  WinChatPresentationApplication::ptr presentation_;
  ChatAetherRuntime aether_runtime_;
  ChatSharedBinding shared_;
  HWND dispatcher_{nullptr};
  DWORD ui_thread_{0};
  bool exiting_{false};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_CHAT_WIN_APP_H_
