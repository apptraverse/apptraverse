#include "minimal_app_android_pending_save_race_probe.h"

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>

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
bool run_model_ready = false;
int persist_completed = 0;
std::filesystem::path probe_state_dir;

}  // namespace

std::string RunAndroidPendingBackgroundSaveRaceProbe() {
  probe_state_dir = std::filesystem::temp_directory_path() /
                    "minimal_app_pending_save_race_probe";
  std::error_code ec;
  std::filesystem::remove_all(probe_state_dir, ec);
  std::filesystem::create_directories(probe_state_dir, ec);

  ae::RamDomainStorage user_storage;
  apptraverse::ApplicationModelRuntime runtime;
  apptraverse::ui::android::AndroidPresentationHost presentation_host{};
  presentation_host.content_root = reinterpret_cast<jobject>(1);

  run_model_ready = false;
  persist_completed = 0;

  apptraverse::android::AndroidApplicationHostCallbacks callbacks;
  callbacks.run_model =
      [&runtime, &user_storage](apptraverse::ApplicationPublishedCallback on_published) {
        {
          std::lock_guard<std::mutex> lock{phase_mu};
          run_model_ready = true;
        }
        phase_cv.notify_all();
        return RunMinimalApplicationModel(user_storage, probe_state_dir, runtime,
                                          std::move(on_published));
      };
  callbacks.persist_model = [&user_storage](ae::Domain& domain) -> int {
    WaitAndroidCheckpointPersistTestGateIfEnabled();
    int const result =
        PersistAndroidMinimalUser(domain, user_storage, probe_state_dir);
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

  DisableAndroidCheckpointPersistTestGate();
  EnableAndroidCheckpointPersistTestGate();

  host.StartModelThread();

  {
    std::unique_lock<std::mutex> lock{phase_mu};
    bool const ready = phase_cv.wait_for(
        lock, std::chrono::seconds{30}, [] { return run_model_ready; });
    if (!ready) {
      runtime.RequestStop();
      host.JoinModelThreadForRelease();
      DisableAndroidCheckpointPersistTestGate();
      return "FAIL:run_model_not_ready";
    }
  }

  host.RequestBackgroundSave();

  if (!WaitAndroidCheckpointPersistGateEntriesAtLeast(
          1, std::chrono::seconds{30})) {
    runtime.RequestStop();
    host.JoinModelThreadForRelease();
    DisableAndroidCheckpointPersistTestGate();
    return "FAIL:first_persist_never_entered_gate";
  }

  host.RequestBackgroundSave();

  OpenAndroidCheckpointPersistTestGate();

  {
    std::unique_lock<std::mutex> lock{phase_mu};
    bool const done = phase_cv.wait_for(
        lock, std::chrono::seconds{10},
        [] { return persist_completed >= 2; });
    if (!done) {
      runtime.RequestStop();
      host.JoinModelThreadForRelease();
      DisableAndroidCheckpointPersistTestGate();
      return "FAIL:persist_completed=" + std::to_string(persist_completed);
    }
  }

  runtime.RequestStop();
  host.JoinModelThreadForRelease();
  DisableAndroidCheckpointPersistTestGate();
  return "OK:pending_save_coalesced_to_second_persist";
}

}  // namespace apptraverse::example::minimal
