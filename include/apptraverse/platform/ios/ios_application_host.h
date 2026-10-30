#ifndef APPTRAVERSE_PLATFORM_IOS_IOS_APPLICATION_HOST_H_
#define APPTRAVERSE_PLATFORM_IOS_IOS_APPLICATION_HOST_H_

#include <functional>
#include <optional>
#include <thread>

#include "aether-objects/obj/obj.h"

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/application_ui_session.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/platform/apple/apple_presentation_host.h"

namespace apptraverse::ios {

struct IosApplicationHostCallbacks {
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

class IosApplicationHost {
 public:
  IosApplicationHost(ApplicationModelRuntime& runtime,
                     IosApplicationHostCallbacks callbacks);
  ~IosApplicationHost();

  IosApplicationHost(IosApplicationHost const&) = delete;
  IosApplicationHost& operator=(IosApplicationHost const&) = delete;

  // Main thread. Creates the single UIWindow and model thread. A second call
  // does not start another model runtime.
  void Start();

 private:
  void Deliver(ApplicationPublicationKind kind);
  void OnInitialPublished();
  void OnIncrementalPublished();
  static void OnDesktopWindowUserClose(void* opaque, ae::Obj* desktop_window);

  ApplicationModelRuntime& runtime_;
  IosApplicationHostCallbacks callbacks_;
  ModelObjectProxy model_proxy_;
  ui::apple::ApplePresentationHost presentation_host_{};
  std::optional<ApplicationUiSession> ui_session_;
  std::thread model_thread_;
  void* ui_window_{nullptr};
  void* root_controller_{nullptr};
  bool started_{false};
  bool delivered_initial_{false};
};

}  // namespace apptraverse::ios

#endif  // APPTRAVERSE_PLATFORM_IOS_IOS_APPLICATION_HOST_H_
