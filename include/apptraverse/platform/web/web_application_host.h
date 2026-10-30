#ifndef APPTRAVERSE_PLATFORM_WEB_WEB_APPLICATION_HOST_H_
#define APPTRAVERSE_PLATFORM_WEB_WEB_APPLICATION_HOST_H_

#include <atomic>
#include <functional>
#include <optional>
#include <string>
#include <thread>

#include <emscripten/proxying.h>

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/application_ui_session.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/platform/web/web_presentation_host.h"

namespace apptraverse::web {

struct WebApplicationHostCallbacks {
  using RunModel = std::function<void(ApplicationPublishedCallback)>;

  RunModel run_model;
  std::function<void(ui::web::WebPresentationHost&)> refresh_presentation_host;
  TakePublishedSidecarCallback take_incremental_sidecar;
  std::function<void(ae::Obj& application_root)> after_initial;
  std::function<void(ae::Obj& application_root)> after_incremental;
  std::function<void()> request_stop;
  std::function<int(ae::Domain& domain)> persist_model;
  std::function<void()> on_save_started;
  std::function<void()> on_save_succeeded;
  std::function<void()> on_save_failed;
  std::function<void(bool blocked)> set_ui_input_blocked;
};

class WebApplicationHost {
 public:
  WebApplicationHost(ApplicationModelRuntime& runtime,
                     ui::web::WebPresentationHost& presentation_host,
                     WebApplicationHostCallbacks callbacks);
  ~WebApplicationHost();

  WebApplicationHost(WebApplicationHost const&) = delete;
  WebApplicationHost& operator=(WebApplicationHost const&) = delete;

  void Start();
  void StartWithPersistentStorage(std::string idbfs_mount_path);

  void RequestSave();

  ApplicationUiSession* ui_session() {
    return ui_session_ ? &*ui_session_ : nullptr;
  }

 private:
  enum class SaveState : std::uint8_t {
    kIdle,
    kSaving,
  };

  ApplicationPublishedCallback ModelThreadPublicationBridge();
  void HandlePublicationOnMainThread(ApplicationPublicationKind kind);
  void EnqueueModelWork(ModelObjectProxy::ModelWork work);
  void FinishSaveFailure();
  void FinishSaveSuccess();

  ApplicationModelRuntime& runtime_;
  ui::web::WebPresentationHost& presentation_host_;
  WebApplicationHostCallbacks callbacks_;
  ModelObjectProxy model_proxy_;
  emscripten::ProxyingQueue proxy_queue_;
  std::optional<ApplicationUiSession> ui_session_;
  std::thread model_thread_;
  std::atomic<SaveState> save_state_{SaveState::kIdle};
  std::string idbfs_mount_path_;
};

}  // namespace apptraverse::web

#endif  // APPTRAVERSE_PLATFORM_WEB_WEB_APPLICATION_HOST_H_
