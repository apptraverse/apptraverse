#include "apptraverse/platform/android/android_application_host.h"

#include <vector>

#include "android_log.h"

#include "apptraverse/noninteractive_crt.h"

namespace apptraverse::android {
namespace {

void DeleteGlobalRef(JavaVM* vm, jobject& ref) {
  if (vm == nullptr || ref == nullptr) {
    ref = nullptr;
    return;
  }
  JNIEnv* env = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) {
    env->DeleteGlobalRef(ref);
  }
  ref = nullptr;
}

}  // namespace

void* AndroidApplicationHost::ModelThreadTrampoline(void* arg) {
  static_cast<AndroidApplicationHost*>(arg)->RunModelWorkerThread();
  return nullptr;
}

AndroidApplicationHost::AndroidApplicationHost(
    ApplicationModelRuntime& runtime,
    ui::android::AndroidPresentationHost& presentation_host,
    AndroidUiBridge ui_bridge, AndroidApplicationHostCallbacks callbacks)
    : runtime_{runtime},
      presentation_host_{presentation_host},
      ui_bridge_{std::move(ui_bridge)},
      callbacks_{std::move(callbacks)},
      model_proxy_{[this](ModelObjectProxy::ModelWork work) {
        EnqueueModelWork(std::move(work));
      }} {}

AndroidApplicationHost::~AndroidApplicationHost() {
  if (model_thread_live_) {
    WriteFatalStderr(
        "fatal: AndroidApplicationHost destroyed before model thread joined\n");
    std::abort();
  }
  DeleteGlobalRef(presentation_host_.vm, presentation_host_.activity);
  DeleteGlobalRef(presentation_host_.vm, presentation_host_.content_root);
}

void AndroidApplicationHost::SetPresentationRoot(JNIEnv* env, jobject activity,
                                                 jobject content_root) {
  if (presentation_host_.vm == nullptr) {
    env->GetJavaVM(&presentation_host_.vm);
  }
  DeleteGlobalRef(presentation_host_.vm, presentation_host_.activity);
  DeleteGlobalRef(presentation_host_.vm, presentation_host_.content_root);
  presentation_host_.activity = env->NewGlobalRef(activity);
  presentation_host_.content_root = env->NewGlobalRef(content_root);
  if (callbacks_.refresh_presentation_host) {
    callbacks_.refresh_presentation_host(presentation_host_);
  }
}

void AndroidApplicationHost::EnqueueModelWork(ModelObjectProxy::ModelWork work) {
  runtime_.Post(std::move(work));
}

ApplicationPublishedCallback AndroidApplicationHost::ModelThreadPublicationBridge() {
  return [this](ApplicationPublicationKind kind) {
    ui_bridge_.PostPublication(
        kind == ApplicationPublicationKind::Initial ? 0 : 1);
  };
}

void AndroidApplicationHost::RunModelWorkerThread() {
  (void)callbacks_.run_model(ModelThreadPublicationBridge());
  LogInfo("MINIMAL_APP_ANDROID_MODEL_THREAD_EXIT");
}

void AndroidApplicationHost::StartModelThread() {
  if (model_thread_live_) {
    WriteFatalStderr(
        "fatal: AndroidApplicationHost::StartModelThread already live\n");
    std::abort();
  }
  if (presentation_host_.content_root == nullptr) {
    WriteFatalStderr(
        "fatal: AndroidApplicationHost start without presentation root\n");
    std::abort();
  }
  int const create_rc =
      pthread_create(&model_thread_, nullptr, ModelThreadTrampoline, this);
  if (create_rc != 0) {
    WriteFatalStderr("fatal: AndroidApplicationHost pthread_create failed\n");
    std::abort();
  }
  model_thread_live_ = true;
}

void AndroidApplicationHost::HandlePublicationOnMainThread(
    ApplicationPublicationKind kind) {
  if (kind == ApplicationPublicationKind::Initial) {
    std::vector<std::uint8_t> bytes = runtime_.TakePublishedCopy();
    runtime_.NotifyPublicationConsumed();
    ui_session_.emplace(bytes, &presentation_host_, model_proxy_);
    if (callbacks_.after_initial) {
      callbacks_.after_initial(ui_session_->root());
    }
    return;
  }
  if (!ui_session_) {
    WriteFatalStderr("fatal: incremental publication before initial\n");
    std::abort();
  }
  std::vector<std::uint8_t> bytes = runtime_.TakePublishedCopy();
  runtime_.NotifyPublicationConsumed();
  ui_session_->ApplyStructural(bytes);
  if (callbacks_.after_incremental) {
    callbacks_.after_incremental(ui_session_->root());
  }
}

void AndroidApplicationHost::ConsumePublication(ApplicationPublicationKind kind) {
  HandlePublicationOnMainThread(kind);
}

}  // namespace apptraverse::android
