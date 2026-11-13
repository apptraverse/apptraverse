#include "minimal_app_android_lifecycle.h"

#include "minimal_application.h"

#include <filesystem>

#include "android_log.h"

#include "apptraverse/application_model_session.h"
#include "apptraverse/embedded_application_model_session.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/single_file_user_storage.h"
#include "apptraverse/platform/android/android_ui_registration.h"
#include "apptraverse/ui/ui_presenters.h"

#include "embedded_distilled_minimal_app_supply.h"
#include "minimal_app_persistence.h"
#include "minimal_app_prepare.h"

namespace apptraverse::example::minimal {
int PersistAndroidMinimalUser(ae::Domain& domain, ae::RamDomainStorage& user_storage,
                              std::filesystem::path const& state_dir) {
  auto application = domain.Find(MinimalApplicationRootId()).as<MinimalApplication>();
  int const saved = SaveMinimalApplicationEmbeddedUserState(*application);
  if (saved != 0) return saved;
  return SaveUserStorageFile(user_storage, state_dir) ? 0 : 1;
}

int RunMinimalApplicationModel(ae::RamDomainStorage& user_storage,
                               std::filesystem::path const& state_dir,
                               ApplicationModelRuntime& runtime,
                               ApplicationPublishedCallback on_published) {
  EnsureObjectRegistration();
  ui::EnsureUiPresenterRegistration();
  ui::android::EnsureAndroidUiPresenterRegistration();
  EnsureMinimalApplicationRegistration();

  if (!LoadUserStorageFile(user_storage, state_dir)) return 1;
  bool const user_profile_present = !user_storage.state.empty();
  if (user_profile_present) {
    apptraverse::android::LogInfo("MINIMAL_APP_LOADED_FROM_PROFILE");
  } else {
    apptraverse::android::LogInfo("MINIMAL_APP_LOADED_FROM_EMBEDDED_BACKING");
  }

  ApplicationModelSessionHooks<MinimalApplication> hooks;
  hooks.prepare = PrepareMinimalApplicationForRun;
  hooks.persist = [&user_storage, &state_dir](MinimalApplication& application) -> int {
    return PersistAndroidMinimalUser(*application.domain, user_storage, state_dir);
  };

  return RunEmbeddedApplicationModelSession(
      user_storage, embedded_distilled::minimal::kEmbeddedGraph, runtime,
      std::move(on_published), hooks);
}

}  // namespace apptraverse::example::minimal
