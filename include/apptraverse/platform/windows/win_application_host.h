#ifndef APPTRAVERSE_PLATFORM_WINDOWS_WIN_APPLICATION_HOST_H_
#define APPTRAVERSE_PLATFORM_WINDOWS_WIN_APPLICATION_HOST_H_

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <functional>
#include <optional>

#include "aether-objects/obj/obj.h"

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/application_ui_session.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/platform/windows/win_application_loading_window.h"
#include "apptraverse/platform/windows/win_application_message_host.h"
#include "apptraverse/platform/windows/win_presentation_host.h"

namespace apptraverse {
namespace windows {

struct WinApplicationLoadingOptions {
  wchar_t const* class_name;
  wchar_t const* title;
  wchar_t const* message;
  int x;
  int y;
  int width;
  int height;
};

struct WinApplicationHostCallbacks {
  using RunModel = std::function<void(ApplicationPublishedCallback)>;

  RunModel run_model;
  std::function<void()> request_stop;
  std::function<void(ae::Obj& application_root, ae::Obj& closed_window,
                     ModelObjectProxy& proxy)>
      on_desktop_window_close;
  TakePublishedSidecarCallback take_incremental_sidecar;
  std::function<void(ae::Obj& application_root)> after_incremental;
};

class WinApplicationHost {
 public:
  WinApplicationHost(ApplicationModelRuntime& runtime,
                     WinApplicationLoadingOptions loading,
                     WinApplicationHostCallbacks callbacks);
  ~WinApplicationHost();

  WinApplicationHost(WinApplicationHost const&) = delete;
  WinApplicationHost& operator=(WinApplicationHost const&) = delete;

  void Run();

 private:
  void OnInitialPublished();
  void OnIncrementalPublished();

  static void OnDesktopWindowUserClose(void* opaque, ae::Obj* desktop_window);

  ApplicationModelRuntime& runtime_;
  WinApplicationHostCallbacks callbacks_;
  ModelObjectProxy model_proxy_;
  ui::windows::WinPresentationHost presentation_host_{};
  WinApplicationLoadingWindow loading_;
  WinApplicationMessageHost message_host_;
  std::optional<ApplicationUiSession> ui_session_;
};

}  // namespace windows
}  // namespace apptraverse

#endif  // APPTRAVERSE_PLATFORM_WINDOWS_WIN_APPLICATION_HOST_H_
