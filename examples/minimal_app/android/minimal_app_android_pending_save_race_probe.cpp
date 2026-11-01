#include "minimal_app_android_pending_save_race_probe.h"

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <thread>

#include "minimal_app_android_checkpoint_test_gate.h"
#include "minimal_app_persistence.h"

#include "minimal_app_android_lifecycle.h"

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/platform/android/android_application_host.h"
#include "apptraverse/platform/android/android_presentation_host.h"

namespace apptraverse::example::minimal {
namespace {

std::mutex phase_mu;
std::condition_variable phase_cv;
bool allow_second_request = false;
bool second_request_sent = false;
int persist_completed = 0;
std::filesystem::path probe_state_dir;

}  // namespace

std::string RunAndroidPendingBackgroundSaveRaceProbe() {
  probe_state_dir = std::filesystem::temp_directory_path() /
                    "minimal_app_pending_save_race_probe";
  std::error_code ec;
  std::filesystem::remove_all(probe_state_dir, ec);
  std::filesystem::create_directories(probe_state_dir, ec);

  apptraverse::ApplicationModelRuntime runtime;
  apptraverse::ui::android::AndroidPresentationHost presentation_host{};
  presentation_host.content_root = reinterpret_cast<jobject>(1);

  apptraverse::android::AndroidApplicationHostCallbacks callbacks;
  callbacks.run_model =
      [&runtime](apptraverse::ApplicationPublishedCallback on_published) {
        return RunMinimalApplicationModel(probe_state_dir, runtime,
                                          std::move(on_published), true);
      };
  callbacks.persist_model = [](ae::Domain& domain) -> int {
    WaitAndroidCheckpointPersistTestGateIfEnabled();
    int const result = PersistMinimalApplicationFromDomain(domain, probe_state_dir);
    {
      std::lock_guard<std::mutex> lock{phase_mu};
      ++persist_completed;
    }
    phase_cv.notify_all();
    return result;
  };

  apptraverse::android::AndroidUiBridge bridge;
  apptraverse::android::AndroidApplicationHost host(
      runtime, presentation_host, std::move(bridge), std::move(callbacks));

  allow_second_request = false;
  second_request_sent = false;
  persist_completed = 0;
  DisableAndroidCheckpointPersistTestGate();
  EnableAndroidCheckpointPersistTestGate();

  host.StartModelThread();

  bool first_persist_entered = false;
  auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds{30};
  while (std::chrono::steady_clock::now() < deadline) {
    host.RequestBackgroundSave();
    if (WaitAndroidCheckpointPersistGateEntriesAtLeast(
            1, std::chrono::milliseconds{250})) {
      first_persist_entered = true;
      break;
    }
  }
  if (!first_persist_entered) {
    runtime.RequestStop();
    host.JoinModelThreadForRelease();
    DisableAndroidCheckpointPersistTestGate();
    return "FAIL:first_persist_never_entered_gate";
  }

  {
    std::lock_guard<std::mutex> lock{phase_mu};
    allow_second_request = true;
  }
  phase_cv.notify_all();

  std::thread second_request{[&host] {
    std::unique_lock<std::mutex> lock{phase_mu};
    phase_cv.wait(lock, [] { return allow_second_request; });
    second_request_sent = true;
    lock.unlock();
    host.RequestBackgroundSave();
  }};

  OpenAndroidCheckpointPersistTestGate();

  {
    std::unique_lock<std::mutex> lock{phase_mu};
    bool const done = phase_cv.wait_for(
        lock, std::chrono::seconds{10},
        [] { return persist_completed >= 2; });
    if (!done) {
      second_request.join();
      runtime.RequestStop();
      host.JoinModelThreadForRelease();
      DisableAndroidCheckpointPersistTestGate();
      return "FAIL:persist_completed=" + std::to_string(persist_completed);
    }
  }

  second_request.join();
  if (!second_request_sent) {
    runtime.RequestStop();
    host.JoinModelThreadForRelease();
    DisableAndroidCheckpointPersistTestGate();
    return "FAIL:second_request_not_sent";
  }

  runtime.RequestStop();
  host.JoinModelThreadForRelease();
  DisableAndroidCheckpointPersistTestGate();
  return "OK:pending_save_coalesced_to_second_persist";
}

}  // namespace apptraverse::example::minimal
