#include "minimal_app_android_lifecycle.h"

#include "minimal_application.h"

#include <filesystem>

#include "android_log.h"

#include "apptraverse/application_model_session.h"
#include "apptraverse/embedded_application_model_session.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/platform/android/android_ui_registration.h"
#include "apptraverse/ui/ui_presenters.h"

#include "embedded_distilled_minimal_app_supply.h"
#include "minimal_app_persistence.h"
#include "minimal_app_prepare.h"

namespace apptraverse::example::minimal {
namespace {

bool UserProfilePresent(std::filesystem::path const& state_dir) {
  return std::filesystem::exists(state_dir) &&
         !std::filesystem::is_empty(state_dir);
}

}  // namespace

int RunMinimalApplicationModel(DirectoryDomainStorage& user_storage,
                               ApplicationModelRuntime& runtime,
                               ApplicationPublishedCallback on_published) {
  EnsureObjectRegistration();
  ui::EnsureUiPresenterRegistration();
  ui::android::EnsureAndroidUiPresenterRegistration();
  EnsureMinimalApplicationRegistration();

  bool const user_profile_present = UserProfilePresent(user_storage.root());
  if (user_profile_present) {
    apptraverse::android::LogInfo("MINIMAL_APP_LOADED_FROM_PROFILE");
  } else {
    apptraverse::android::LogInfo("MINIMAL_APP_LOADED_FROM_EMBEDDED_BACKING");
  }

  ApplicationModelSessionHooks<MinimalApplication> hooks;
  hooks.prepare = [user_profile_present](MinimalApplication& application) {
    PrepareMinimalApplicationForRun(application);
    if (user_profile_present) {
      NormalizeMinimalApplicationAfterEmbeddedUserLoad(application);
    }
  };
  hooks.persist = [&user_storage](MinimalApplication& application) -> int {
    return PersistMinimalApplicationEmbeddedUserOverlay(application,
                                                        user_storage);
  };

  return RunEmbeddedApplicationModelSession(
      user_storage, embedded_distilled::minimal::kEmbeddedGraph, runtime,
      std::move(on_published), hooks);
}

}  // namespace apptraverse::example::minimal
