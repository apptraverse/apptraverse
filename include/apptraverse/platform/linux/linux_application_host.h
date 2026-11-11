#ifndef APPTRAVERSE_PLATFORM_LINUX_LINUX_APPLICATION_HOST_H_
#define APPTRAVERSE_PLATFORM_LINUX_LINUX_APPLICATION_HOST_H_

#include <atomic>
#include <functional>
#include <optional>
#include <thread>

#include "aether-objects/obj/obj.h"

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/application_ui_session.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/platform/linux/linux_presentation_host.h"

typedef int gboolean;
typedef void* gpointer;

namespace apptraverse {
namespace linux {

gboolean LinuxApplicationHostPublicationIdle(gpointer data);

struct LinuxApplicationHostCallbacks {
  using RunModel = std::function<void(ApplicationPublishedCallback)>;

  RunModel run_model;
  std::function<void()> request_stop;
  std::function<void(ae::Obj& application_root, ae::Obj& closed_window,
                     ModelObjectProxy& proxy)>
      on_desktop_window_close;
  TakePublishedSidecarCallback take_incremental_sidecar;
  std::function<void(ae::Obj& application_root)> after_incremental;
  std::function<void(ae::Obj& application_root)> after_initial;
  UiPresentersPrepareHook prepare_ui_presenters;
};

class LinuxApplicationHost {
  friend gboolean LinuxApplicationHostPublicationIdle(gpointer data);

 public:
  explicit LinuxApplicationHost(ApplicationModelRuntime& runtime,
                                LinuxApplicationHostCallbacks callbacks);
  ~LinuxApplicationHost();

  LinuxApplicationHost(LinuxApplicationHost const&) = delete;
  LinuxApplicationHost& operator=(LinuxApplicationHost const&) = delete;

  void Run();

 private:
  void SchedulePublication(ApplicationPublicationKind kind);
  void HandlePublication(ApplicationPublicationKind kind);
  void OnInitialPublished();
  void OnIncrementalPublished();

  static void OnDesktopWindowUserClose(void* opaque, ae::Obj* desktop_window);

  ApplicationModelRuntime& runtime_;
  LinuxApplicationHostCallbacks callbacks_;
  ModelObjectProxy model_proxy_;
  ui::linux::LinuxPresentationHost presentation_host_{};
  std::optional<ApplicationUiSession> ui_session_;
  std::thread model_thread_;
  std::atomic<bool> model_thread_done_{false};
};

}  // namespace linux
}  // namespace apptraverse

#endif  // APPTRAVERSE_PLATFORM_LINUX_LINUX_APPLICATION_HOST_H_
