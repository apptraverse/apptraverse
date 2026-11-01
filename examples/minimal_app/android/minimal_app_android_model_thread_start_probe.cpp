#include "minimal_app_android_model_thread_start_probe.h"

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

}  // namespace

std::string RunAndroidModelThreadStartSyncProbe() {
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

  std::string result = "FAIL:unknown";
  std::thread starter{[&host, &result] {
    host.StartModelThread();
    result = "OK:start_unblocked";
  }};

  {
    std::unique_lock<std::mutex> lock{run_model_mu};
    bool const entered = run_model_cv.wait_for(
        lock, std::chrono::seconds(5), [] { return run_model_entered; });
    if (!entered) {
      release_run_model = true;
      run_model_cv.notify_all();
      starter.join();
      host.JoinModelThreadForRelease();
      return "FAIL:worker_never_entered";
    }
  }

  starter.join();
  if (result != "OK:start_unblocked") {
    release_run_model = true;
    run_model_cv.notify_all();
    host.JoinModelThreadForRelease();
    return result;
  }

  release_run_model = true;
  run_model_cv.notify_all();
  host.JoinModelThreadForRelease();
  return "OK:start_unblocked";
}

}  // namespace apptraverse::example::minimal
