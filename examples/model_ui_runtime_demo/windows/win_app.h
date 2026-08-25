#ifndef APPTRAVERSE_WIN_APP_H_
#define APPTRAVERSE_WIN_APP_H_

#include <memory>

#include "apptraverse/model_runtime.h"
#include "apptraverse/ui_mirror.h"

#include "demo_bootstrap.h"
#include "win_presenters.h"

namespace apptraverse {

class WinApp {
 public:
  int Run(std::filesystem::path const& state_dir);

  void OnPublished(std::uint32_t root_id, PublicationChannel<3>* channel);

 private:
  void ApplyPublication(std::uint32_t root_id, PublicationChannel<3>* channel);
  void RequestExit();

  DemoRuntime runtime_;
  std::unique_ptr<UiMirror> ui_mirror_;
  std::unique_ptr<ModelRuntime> model_runtime_;
  WinPresentationApplication::ptr presentation_;
  HWND dispatcher_{nullptr};
  DWORD ui_thread_{0};
  bool exiting_{false};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_WIN_APP_H_
