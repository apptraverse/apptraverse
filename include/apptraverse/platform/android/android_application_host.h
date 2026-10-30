#ifndef APPTRAVERSE_PLATFORM_ANDROID_ANDROID_APPLICATION_HOST_H_
#define APPTRAVERSE_PLATFORM_ANDROID_ANDROID_APPLICATION_HOST_H_

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <pthread.h>

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

  ApplicationModelRuntime& runtime_;
  ui::android::AndroidPresentationHost& presentation_host_;
  AndroidUiBridge ui_bridge_;
  AndroidApplicationHostCallbacks callbacks_;
  ModelObjectProxy model_proxy_;
  std::optional<ApplicationUiSession> ui_session_;
  pthread_t model_thread_{};
  bool model_thread_live_{false};
};

}  // namespace apptraverse::android

#endif  // APPTRAVERSE_PLATFORM_ANDROID_ANDROID_APPLICATION_HOST_H_
