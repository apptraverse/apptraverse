#ifndef APPTRAVERSE_PLATFORM_ANDROID_ANDROID_APPLICATION_HOST_H_
#define APPTRAVERSE_PLATFORM_ANDROID_ANDROID_APPLICATION_HOST_H_

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <pthread.h>

#include "aether-objects/obj/domain.h"

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/application_ui_session.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/platform/android/android_presentation_host.h"
#include "apptraverse/platform/android/android_ui_bridge.h"

namespace apptraverse::android {

struct AndroidApplicationHostCallbacks {
  using RunModel = std::function<int(ApplicationPublishedCallback)>;

  RunModel run_model;
  std::function<void(ui::android::AndroidPresentationHost&)> refresh_presentation_host;
  std::function<void(ae::Obj& application_root)> after_initial;
  std::function<void(ae::Obj& application_root)> after_incremental;
  // Model-thread checkpoint persist (after queued UI work), same contract as Web.
  std::function<int(ae::Domain& domain)> persist_model;
};

class AndroidApplicationHost {
 public:
  AndroidApplicationHost(ApplicationModelRuntime& runtime,
                         ui::android::AndroidPresentationHost& presentation_host,
                         AndroidUiBridge ui_bridge,
                         AndroidApplicationHostCallbacks callbacks);
  ~AndroidApplicationHost();

  AndroidApplicationHost(AndroidApplicationHost const&) = delete;
  AndroidApplicationHost& operator=(AndroidApplicationHost const&) = delete;

  void SetPresentationRoot(JNIEnv* env, jobject activity, jobject content_root);
  void StartModelThread();
  void ConsumePublication(ApplicationPublicationKind kind);

  // Queues checkpoint persist on the model thread; completion on the main looper.
  void RequestBackgroundSave();

  // RequestStop + join for test/process cold reload (not a lifecycle shutdown API).
  void JoinModelThreadForRelease();

  ApplicationUiSession* ui_session() {
    return ui_session_ ? &*ui_session_ : nullptr;
  }

  ModelObjectProxy& model_proxy() { return model_proxy_; }

 private:
  static void* ModelThreadTrampoline(void* arg);
  void RunModelWorkerThread();
  ApplicationPublishedCallback ModelThreadPublicationBridge();
  void HandlePublicationOnMainThread(ApplicationPublicationKind kind);
  void EnqueueModelWork(ModelObjectProxy::ModelWork work);

  enum class SaveState : std::uint8_t {
    kIdle,
    kSaving,
  };

  ApplicationModelRuntime& runtime_;
  ui::android::AndroidPresentationHost& presentation_host_;
  AndroidUiBridge ui_bridge_;
  AndroidApplicationHostCallbacks callbacks_;
  ModelObjectProxy model_proxy_;
  std::optional<ApplicationUiSession> ui_session_;
  void EnqueueBackgroundSaveWork();
  void RunBackgroundSaveOnModelThread(ae::Domain& domain);

  pthread_t model_thread_{};
  std::mutex model_start_mu_;
  std::condition_variable model_start_cv_;
  bool model_worker_entered_{false};
  bool model_thread_joinable_{false};
  std::atomic<bool> model_thread_live_{false};
  std::atomic<SaveState> save_state_{SaveState::kIdle};
  std::atomic<bool> pending_background_save_{false};
};

}  // namespace apptraverse::android

#endif  // APPTRAVERSE_PLATFORM_ANDROID_ANDROID_APPLICATION_HOST_H_
