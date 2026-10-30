#ifndef APPTRAVERSE_PLATFORM_MACOS_MAC_APPLICATION_HOST_H_
#define APPTRAVERSE_PLATFORM_MACOS_MAC_APPLICATION_HOST_H_

#include <functional>
#include <optional>
#include <thread>

#include "aether-objects/obj/obj.h"

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/application_ui_session.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/platform/apple/apple_presentation_host.h"

namespace apptraverse::macos {

struct MacApplicationHostCallbacks {
  using RunModel = std::function<void(ApplicationPublishedCallback)>;

  RunModel run_model;
  std::function<void()> request_stop;
  std::function<void(ae::Obj& application_root, ae::Obj& closed_window,
                     ModelObjectProxy& proxy)>
      on_desktop_window_close;
  TakePublishedSidecarCallback take_incremental_sidecar;
  std::function<void(ae::Obj& application_root)> after_initial;
  std::function<void(ae::Obj& application_root)> after_incremental;
  std::function<void(std::function<void(int status)>)> request_persist;
};

class MacApplicationHost {
 public:
  MacApplicationHost(ApplicationModelRuntime& runtime,
                     MacApplicationHostCallbacks callbacks);
  ~MacApplicationHost();

  MacApplicationHost(MacApplicationHost const&) = delete;
  MacApplicationHost& operator=(MacApplicationHost const&) = delete;

  void Run();
  void FocusWindow(std::uint64_t window_obj_id);
  void RequestApplicationStop();

 private:
  void Deliver(ApplicationPublicationKind kind);
  void OnInitialPublished();
  void OnIncrementalPublished();
  static void OnDesktopWindowUserClose(void* opaque, ae::Obj* desktop_window);

  ApplicationModelRuntime& runtime_;
  MacApplicationHostCallbacks callbacks_;
  ModelObjectProxy model_proxy_;
  ui::apple::ApplePresentationHost presentation_host_{};
  std::optional<ApplicationUiSession> ui_session_;
  std::thread model_thread_;
  void* app_delegate_{nullptr};
  bool delivered_initial_{false};
};

}  // namespace apptraverse::macos

#endif  // APPTRAVERSE_PLATFORM_MACOS_MAC_APPLICATION_HOST_H_
