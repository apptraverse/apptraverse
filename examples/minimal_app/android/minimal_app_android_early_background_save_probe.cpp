#include "minimal_app_android_early_background_save_probe.h"

#include "minimal_app_android_lifecycle.h"
#include "minimal_app_persistence.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/platform/android/android_application_host.h"
#include "apptraverse/platform/android/android_presentation_host.h"

namespace apptraverse::example::minimal {
namespace {

std::mutex ready_mu;
std::condition_variable ready_cv;
bool run_model_ready = false;
std::atomic<int> persist_calls{0};

}  // namespace

std::string RunAndroidEarlyBackgroundSaveProbe() {
  std::filesystem::path const state_dir =
      std::filesystem::temp_directory_path() /
      "minimal_app_early_background_save_probe";
  std::error_code ec;
  std::filesystem::remove_all(state_dir, ec);
  std::filesystem::create_directories(state_dir, ec);

  ae::RamDomainStorage user_storage;
  apptraverse::ApplicationModelRuntime runtime;
  apptraverse::ui::android::AndroidPresentationHost presentation_host{};
  presentation_host.content_root = reinterpret_cast<jobject>(1);

  persist_calls.store(0);
  run_model_ready = false;

  apptraverse::android::AndroidApplicationHostCallbacks callbacks;
  callbacks.run_model =
      [&runtime, &user_storage, &state_dir](apptraverse::ApplicationPublishedCallback on_published) {
        {
          std::lock_guard<std::mutex> lock{ready_mu};
          run_model_ready = true;
        }
        ready_cv.notify_all();
        return RunMinimalApplicationModel(user_storage, state_dir, runtime,
                                          std::move(on_published));
      };
  callbacks.persist_model = [&user_storage, &state_dir](ae::Domain& domain) -> int {
    int const result =
        PersistAndroidMinimalUser(domain, user_storage, state_dir);
    persist_calls.fetch_add(1, std::memory_order_release);
    ready_cv.notify_all();
    return result;
  };

  apptraverse::android::AndroidUiBridge bridge;
  apptraverse::android::AndroidApplicationHost host(
      runtime, presentation_host, std::move(bridge), std::move(callbacks));

  host.RequestBackgroundSave();

  host.StartModelThread();

  {
    std::unique_lock<std::mutex> lock{ready_mu};
    bool const flushed = ready_cv.wait_for(
        lock, std::chrono::seconds{30}, [] {
          return persist_calls.load(std::memory_order_acquire) >= 1;
        });
    if (!flushed) {
      runtime.RequestStop();
      host.JoinModelThreadForRelease();
      return "FAIL:early_pending_not_flushed";
    }
  }

  runtime.RequestStop();
  host.JoinModelThreadForRelease();
  return "OK:early_onstop_pending_flushed_after_live";
}

}  // namespace apptraverse::example::minimal
