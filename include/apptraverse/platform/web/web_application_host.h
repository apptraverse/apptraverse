#ifndef APPTRAVERSE_PLATFORM_WEB_WEB_APPLICATION_HOST_H_
#define APPTRAVERSE_PLATFORM_WEB_WEB_APPLICATION_HOST_H_

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include <pthread.h>

#include <emscripten/proxying.h>

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/application_ui_session.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/platform/web/web_presentation_host.h"

namespace apptraverse::web {

enum class WebApplicationHostLifecycle : std::uint8_t {
  kRunning,
  kStopping,
  kStopped,
};

struct WebApplicationHostCallbacks {
  using RunModel = std::function<int(ApplicationPublishedCallback)>;

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
  std::function<void()> on_shutdown_stopping;
  std::function<void()> on_shutdown_succeeded;
  std::function<void()> on_shutdown_failed;
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
  void RequestShutdown();

  [[nodiscard]] WebApplicationHostLifecycle lifecycle() const {
    return lifecycle_.load(std::memory_order_acquire);
  }

  ApplicationUiSession* ui_session() {
    return ui_session_ ? &*ui_session_ : nullptr;
  }

  ModelObjectProxy& model_proxy() { return model_proxy_; }

  // Invoked from emscripten_async_call after shutdown IDBFS flush.
  void TryJoinModelThreadOnce();

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
  void BeginModelStopAfterSaveBarrier();
  void PrepareShutdownUiOnMainThread(int persist_result);
  void CompleteShutdownFlushOnMainThread();
  void ReleaseModelWorkerAfterMainTeardown();
  void ScheduleTryJoinModelThread();
  void FinishShutdownSuccess();
  void FinishShutdownFailure();
  void RunModelWorkerThread();
  static void* ModelThreadTrampoline(void* arg);

  ApplicationModelRuntime& runtime_;
  ui::web::WebPresentationHost& presentation_host_;
  WebApplicationHostCallbacks callbacks_;
  ModelObjectProxy model_proxy_;
  emscripten::ProxyingQueue proxy_queue_;
  std::optional<ApplicationUiSession> ui_session_;
  pthread_t model_thread_{};
  bool model_thread_live_{false};
  std::atomic<SaveState> save_state_{SaveState::kIdle};
  std::atomic<WebApplicationHostLifecycle> lifecycle_{
      WebApplicationHostLifecycle::kRunning};
  std::atomic<std::uint32_t> worker_main_teardown_done_{0};
  bool shutdown_waiting_for_save_{false};
  bool shutdown_failure_reported_{false};
  bool shutdown_persist_ok_{false};
  bool shutdown_flush_ok_{false};
  std::string idbfs_mount_path_;
};

}  // namespace apptraverse::web

#endif  // APPTRAVERSE_PLATFORM_WEB_WEB_APPLICATION_HOST_H_
