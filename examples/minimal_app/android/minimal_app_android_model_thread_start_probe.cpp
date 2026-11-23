#include "minimal_app_android_model_thread_start_probe.h"

#include "minimal_app_android_lifecycle.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <utility>

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/platform/android/android_application_host.h"
#include "apptraverse/platform/android/android_presentation_host.h"

namespace apptraverse::example::minimal {
namespace {

// Every field is protected by this instance's mutex. The bounded worker wait
// lets a broken Start/Join return a failure instead of stranding the UI owner.
class ModelGate {
 public:
  void EnterAndWait() {
    std::unique_lock<std::mutex> lock{mu_};
    entered_ = true;
    cv_.notify_all();
    if (!cv_.wait_for(lock, std::chrono::seconds{5}, [&] { return released_; }))
      timed_out_ = true;
    exited_ = true;
    cv_.notify_all();
  }
  bool WaitEntered() { return WaitFor(entered_); }
  bool WaitExited() { return WaitFor(exited_); }
  void Release() {
    std::lock_guard<std::mutex> lock{mu_};
    released_ = true;
    cv_.notify_all();
  }
  bool TimedOut() {
    std::lock_guard<std::mutex> lock{mu_};
    return timed_out_;
  }

 private:
  bool WaitFor(bool const& flag) {
    std::unique_lock<std::mutex> lock{mu_};
    return cv_.wait_for(lock, std::chrono::seconds{5}, [&] { return flag; });
  }
  std::mutex mu_;
  std::condition_variable cv_;
  bool entered_{false}, released_{false}, exited_{false}, timed_out_{false};
};

bool BindProbeRoot(JNIEnv* env, jobject root,
                   ui::android::AndroidPresentationHost& presentation) {
  // These scheduling probes never mount presenters or require an Activity.
  // Start still receives a genuine Java View root; the host owns its global
  // reference and releases it on this same attached UI thread.
  if (!root || env->GetJavaVM(&presentation.vm) != JNI_OK) return false;
  presentation.content_root = env->NewGlobalRef(root);
  return presentation.content_root != nullptr && !env->ExceptionCheck();
}

std::string RunStartUnblockedProbe(JNIEnv* env, jobject root) {
  ApplicationModelRuntime runtime;
  ui::android::AndroidPresentationHost presentation;
  if (!BindProbeRoot(env, root, presentation)) return "FAIL:root_reference";
  ModelGate gate;
  android::AndroidApplicationHostCallbacks callbacks;
  callbacks.run_model = [&](ApplicationPublishedCallback) {
    gate.EnterAndWait();
    return 0;
  };
  android::AndroidApplicationHost host{
      runtime, presentation, {}, std::move(callbacks)};
  host.StartModelThread();
  bool const entered = gate.WaitEntered();
  gate.Release();
  host.JoinModelThreadForRelease();
  if (!entered) return "FAIL:worker_never_entered";
  if (gate.TimedOut()) return "FAIL:start_waited_for_session_exit";
  return "OK:start_unblocked";
}

std::string RunFastExitJoinProbe(JNIEnv* env, jobject root) {
  ApplicationModelRuntime runtime;
  ui::android::AndroidPresentationHost presentation;
  if (!BindProbeRoot(env, root, presentation)) return "FAIL:root_reference";
  ModelGate gate;
  gate.Release();
  android::AndroidApplicationHostCallbacks callbacks;
  callbacks.run_model = [&](ApplicationPublishedCallback) {
    gate.EnterAndWait();
    return 0;
  };
  android::AndroidApplicationHost host{
      runtime, presentation, {}, std::move(callbacks)};
  host.StartModelThread();
  bool const exited = gate.WaitExited();
  host.JoinModelThreadForRelease();
  return exited ? "OK:fast_exit_joined" : "FAIL:fast_worker_never_exited";
}

std::string RunJoinBeforeRunModelGateProbe(JNIEnv* env, jobject root) {
  ApplicationModelRuntime runtime;
  ui::android::AndroidPresentationHost presentation;
  if (!BindProbeRoot(env, root, presentation)) return "FAIL:root_reference";
  ModelGate gate;
  android::AndroidApplicationHostCallbacks callbacks;
  callbacks.run_model = [&](ApplicationPublishedCallback) {
    gate.EnterAndWait();
    return 0;
  };
  callbacks.before_model_thread_join = [&] { gate.Release(); };
  android::AndroidApplicationHost host{
      runtime, presentation, {}, std::move(callbacks)};
  host.StartModelThread();
  bool const entered = gate.WaitEntered();
  host.JoinModelThreadForRelease();
  if (!entered) return "FAIL:run_model_gate_never_entered";
  if (gate.TimedOut()) return "FAIL:join_failed_to_release_run_model_gate";
  return "OK:join_released_run_model_gate";
}

std::string RunImmediateOwnerTeardownProbe(JNIEnv* env, jobject root) {
  ApplicationModelRuntime runtime;
  ui::android::AndroidPresentationHost presentation;
  if (!BindProbeRoot(env, root, presentation)) return "FAIL:root_reference";
  ModelGate gate;
  android::AndroidApplicationHostCallbacks callbacks;
  callbacks.run_model = [&](ApplicationPublishedCallback) {
    gate.EnterAndWait();
    return 0;
  };
  callbacks.before_model_thread_join = [&] { gate.Release(); };
  android::AndroidApplicationHost host{
      runtime, presentation, {}, std::move(callbacks)};
  // The UI owner races the worker's entry/exit, rather than racing two callers
  // against host fields that intentionally belong to one UI thread.
  host.StartModelThread();
  host.JoinModelThreadForRelease();
  if (gate.TimedOut()) return "FAIL:immediate_teardown_gate_timeout";
  if (runtime.TryPost([](ae::Domain&) {})) return "FAIL:teardown_accepts_work";
  return "OK:immediate_owner_teardown";
}

std::string RunLongModelTeardownProbe(JNIEnv* env, jobject root,
                                     std::filesystem::path const& state_dir,
                                     jobject ui_bridge) {
  ae::RamDomainStorage user_storage, local_storage;
  ApplicationModelRuntime runtime;
  ui::android::AndroidPresentationHost presentation;
  if (!BindProbeRoot(env, root, presentation)) return "FAIL:root_reference";
  ModelGate gate;
  std::atomic<int> session_result{-1};
  android::AndroidApplicationHostCallbacks callbacks;
  callbacks.run_model = [&](ApplicationPublishedCallback on_published) {
    gate.EnterAndWait();
    session_result.store(RunMinimalApplicationModel(
        user_storage, local_storage, state_dir, runtime, std::move(on_published)));
    return 0; // Report the actual session result through the probe, after join.
  };
  callbacks.before_model_thread_join = [&] { gate.Release(); };
  android::AndroidApplicationHost host{
      runtime, presentation, android::MakeAndroidUiBridge(env, ui_bridge),
      std::move(callbacks)};
  host.StartModelThread();
  bool const entered = gate.WaitEntered();
  host.JoinModelThreadForRelease();
  if (!entered) return "FAIL:long_model_never_entered";
  if (gate.TimedOut()) return "FAIL:long_model_gate_timeout";
  if (session_result.load() != 0) return "FAIL:long_model_persist";
  std::error_code error;
  auto const user_bytes = std::filesystem::file_size(state_dir / "user.bin", error);
  if (error || !user_bytes) return "FAIL:long_model_missing_user";
  auto const local_bytes = std::filesystem::file_size(state_dir / "local/user.bin", error);
  if (error || !local_bytes) return "FAIL:long_model_missing_local";
  return "OK:long_model_joined_and_persisted";
}

}  // namespace

std::string RunAndroidModelThreadStartSyncProbe(
    JNIEnv* env, jobject content_root, std::filesystem::path const& state_dir,
    jobject ui_bridge) {
  for (auto probe : {RunStartUnblockedProbe, RunFastExitJoinProbe,
                     RunJoinBeforeRunModelGateProbe, RunImmediateOwnerTeardownProbe}) {
    auto const result = probe(env, content_root);
    if (result.rfind("OK:", 0) != 0) return result;
  }
  auto const result = RunLongModelTeardownProbe(env, content_root, state_dir, ui_bridge);
  if (result.rfind("OK:", 0) != 0) return result;
  return "OK:start_unblocked_fast_exit_gate_join_owner_teardown_persist";
}

}  // namespace apptraverse::example::minimal
