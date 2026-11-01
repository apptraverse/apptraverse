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

void JoinModelThreadIfJoinable(pthread_t& thread, bool joinable) {
  if (!joinable) {
    return;
  }
  void* join_result = nullptr;
  int const join_rc = pthread_join(thread, &join_result);
  if (join_rc != 0) {
    WriteFatalStderr("fatal: AndroidApplicationHost pthread_join failed\n");
    std::abort();
  }
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
  if (model_thread_joinable_) {
    JoinModelThreadForRelease();
  }
  if (model_thread_live_.load(std::memory_order_acquire)) {
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
  if (ui_session_) {
    ui_session_->RemountPresentation();
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
  {
    std::lock_guard<std::mutex> lock{model_start_mu_};
    model_worker_entered_ = true;
    model_start_cv_.notify_all();
    if (model_teardown_requested_) {
      return;
    }
    model_thread_live_.store(true, std::memory_order_release);
  }

  TryStartBackgroundSaveFromPending();

  {
    std::lock_guard<std::mutex> lock{model_start_mu_};
    if (model_teardown_requested_) {
      model_thread_live_.store(false, std::memory_order_release);
      return;
    }
  }

  (void)callbacks_.run_model(ModelThreadPublicationBridge());

  model_thread_live_.store(false, std::memory_order_release);
  LogInfo("MINIMAL_APP_ANDROID_MODEL_THREAD_EXIT");
}

void AndroidApplicationHost::StartModelThread() {
  if (model_thread_joinable_) {
    WriteFatalStderr(
        "fatal: AndroidApplicationHost::StartModelThread joinable thread\n");
    std::abort();
  }
  if (model_thread_live_.load(std::memory_order_acquire)) {
    WriteFatalStderr(
        "fatal: AndroidApplicationHost::StartModelThread already live\n");
    std::abort();
  }
  if (presentation_host_.content_root == nullptr) {
    WriteFatalStderr(
        "fatal: AndroidApplicationHost start without presentation root\n");
    std::abort();
  }
  {
    std::lock_guard<std::mutex> lock{model_start_mu_};
    model_worker_entered_ = false;
    model_teardown_requested_ = false;
  }
  int const create_rc =
      pthread_create(&model_thread_, nullptr, ModelThreadTrampoline, this);
  if (create_rc != 0) {
    WriteFatalStderr("fatal: AndroidApplicationHost pthread_create failed\n");
    std::abort();
  }
  model_thread_joinable_ = true;
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

void AndroidApplicationHost::JoinModelThreadForRelease() {
  if (!model_thread_joinable_) {
    ui_session_.reset();
    return;
  }
  {
    std::lock_guard<std::mutex> lock{model_start_mu_};
    model_teardown_requested_ = true;
  }
  model_start_cv_.notify_all();
  if (callbacks_.before_model_thread_join) {
    callbacks_.before_model_thread_join();
  }
  runtime_.RequestStop();
  JoinModelThreadIfJoinable(model_thread_, model_thread_joinable_);
  model_thread_joinable_ = false;
  {
    std::lock_guard<std::mutex> lock{model_start_mu_};
    model_worker_entered_ = false;
    model_teardown_requested_ = false;
  }
  model_thread_live_.store(false, std::memory_order_release);
  ui_session_.reset();
}

void AndroidApplicationHost::EnqueueBackgroundSaveWork() {
  runtime_.Post([this](ae::Domain& domain) {
    RunBackgroundSaveOnModelThread(domain);
  });
}

void AndroidApplicationHost::RunBackgroundSaveOnModelThread(ae::Domain& domain) {
  int const persist_result = callbacks_.persist_model(domain);
  ui_bridge_.PostBackgroundSaveFinished(persist_result);
  save_state_.store(SaveState::kIdle, std::memory_order_release);
  if (!pending_background_save_.exchange(false, std::memory_order_acq_rel)) {
    return;
  }
  SaveState expected = SaveState::kIdle;
  if (save_state_.compare_exchange_strong(expected, SaveState::kSaving,
                                          std::memory_order_acq_rel)) {
    EnqueueBackgroundSaveWork();
    return;
  }
  pending_background_save_.store(true, std::memory_order_release);
}

void AndroidApplicationHost::TryStartBackgroundSaveFromPending() {
  if (!callbacks_.persist_model) {
    return;
  }
  if (!pending_background_save_.load(std::memory_order_acquire)) {
    return;
  }
  SaveState expected = SaveState::kIdle;
  if (save_state_.compare_exchange_strong(expected, SaveState::kSaving,
                                          std::memory_order_acq_rel)) {
    pending_background_save_.store(false, std::memory_order_release);
    EnqueueBackgroundSaveWork();
  }
}

void AndroidApplicationHost::RequestBackgroundSave() {
  if (!callbacks_.persist_model) {
    WriteFatalStderr(
        "fatal: AndroidApplicationHost::RequestBackgroundSave without "
        "persist_model\n");
    std::abort();
  }
  pending_background_save_.store(true, std::memory_order_release);
  if (!model_thread_live_.load(std::memory_order_acquire)) {
    return;
  }
  TryStartBackgroundSaveFromPending();
}

}  // namespace apptraverse::android
