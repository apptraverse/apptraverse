#ifndef APPTRAVERSE_WIN_APP_H_
#define APPTRAVERSE_WIN_APP_H_

#include <memory>

#include "demo_bootstrap.h"
#include "model_executor.h"
#include "ui_runtime_registry.h"
#include "win_window_presenter.h"

namespace apptraverse {

class WinApp {
 public:
  int Run(std::filesystem::path const& state_dir);

  void OnPublished(std::uint32_t root_id, PublicationChannel<3>* channel);

 private:
  void ApplyPublication(std::uint32_t root_id, PublicationChannel<3>* channel);
  void CreateWindowsIfNeeded();
  void RequestExit();

  DemoRuntime runtime_;
  std::unique_ptr<ModelExecutor> executor_;
  UiRuntimeRegistry registry_;
  WinWindowPresenter window_a_;
  WinWindowPresenter window_b_;
  HWND dispatcher_{nullptr};
  DWORD ui_thread_{0};
  bool windows_created_{false};
  bool exiting_{false};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_WIN_APP_H_
