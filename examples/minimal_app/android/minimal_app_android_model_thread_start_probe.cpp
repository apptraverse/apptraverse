#include "minimal_app_android_model_thread_start_probe.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/platform/android/android_application_host.h"
#include "apptraverse/platform/android/android_presentation_host.h"

namespace apptraverse::example::minimal {
namespace {

std::mutex run_model_mu;
std::condition_variable run_model_cv;
bool run_model_entered = false;
bool release_run_model = false;

std::string RunStartUnblockedProbe() {
  apptraverse::ApplicationModelRuntime runtime;
  apptraverse::ui::android::AndroidPresentationHost presentation_host{};
  presentation_host.content_root = reinterpret_cast<jobject>(1);

  apptraverse::android::AndroidApplicationHostCallbacks callbacks;
  callbacks.run_model = [](apptraverse::ApplicationPublishedCallback) -> int {
    {
      std::lock_guard<std::mutex> lock{run_model_mu};
      run_model_entered = true;
    }
    run_model_cv.notify_all();
    std::unique_lock<std::mutex> lock{run_model_mu};
    run_model_cv.wait(lock, [] { return release_run_model; });
    return 0;
  };

  apptraverse::android::AndroidUiBridge bridge;
  apptraverse::android::AndroidApplicationHost host(
      runtime, presentation_host, std::move(bridge), std::move(callbacks));

  run_model_entered = false;
  release_run_model = false;

  std::atomic<bool> start_returned{false};
  std::thread starter{[&host, &start_returned] {
    host.StartModelThread();
    start_returned.store(true, std::memory_order_release);
  }};

  starter.join();
  if (!start_returned.load(std::memory_order_acquire)) {
    release_run_model = true;
    run_model_cv.notify_all();
    host.JoinModelThreadForRelease();
    return "FAIL:start_never_returned";
  }

  {
    std::unique_lock<std::mutex> lock{run_model_mu};
    bool const entered = run_model_cv.wait_for(
        lock, std::chrono::seconds(5), [] { return run_model_entered; });
    if (!entered) {
      release_run_model = true;
      run_model_cv.notify_all();
      host.JoinModelThreadForRelease();
      return "FAIL:worker_never_entered";
    }
  }

  release_run_model = true;
  run_model_cv.notify_all();
  host.JoinModelThreadForRelease();
  return "OK:start_unblocked";
}

std::string RunFastExitJoinProbe() {
  apptraverse::ApplicationModelRuntime runtime;
  apptraverse::ui::android::AndroidPresentationHost presentation_host{};
  presentation_host.content_root = reinterpret_cast<jobject>(1);

  apptraverse::android::AndroidApplicationHostCallbacks callbacks;
  callbacks.run_model = [](apptraverse::ApplicationPublishedCallback) -> int {
    return 0;
  };

  apptraverse::android::AndroidUiBridge bridge;
  apptraverse::android::AndroidApplicationHost host(
      runtime, presentation_host, std::move(bridge), std::move(callbacks));

  host.StartModelThread();
  host.JoinModelThreadForRelease();
  return "OK:fast_exit_joined";
}

}  // namespace

std::string RunAndroidModelThreadStartSyncProbe() {
  std::string const unblocked = RunStartUnblockedProbe();
  if (unblocked.rfind("OK:", 0) != 0) {
    return unblocked;
  }
  std::string const fast = RunFastExitJoinProbe();
  if (fast.rfind("OK:", 0) != 0) {
    return fast;
  }
  return "OK:start_unblocked_and_fast_exit_join";
}

}  // namespace apptraverse::example::minimal
