#include "minimal_app_android_lifecycle.h"

#include "minimal_application.h"

#include <filesystem>


#include "apptraverse/application_model_session.h"
#include "apptraverse/embedded_application_model_session.h"
#include "apptraverse/single_file_user_storage.h"

#include "embedded_distilled_minimal_app_supply.h"
#include "minimal_app_persistence.h"
#include "minimal_app_prepare.h"

namespace apptraverse::example::minimal {
int PersistAndroidMinimalUser(ae::Domain& domain, ae::RamDomainStorage& user_storage,
                               ae::RamDomainStorage& local_storage,
                              std::filesystem::path const& state_dir) {
  auto application = domain.Find(MinimalApplicationRootId()).as<MinimalApplication>();
  int const saved = SaveMinimalApplicationEmbeddedUserState(*application);
  if (saved != 0) return saved;
  return SaveUserStorageFile(user_storage, state_dir) &&
      SaveUserStorageFile(local_storage, state_dir / "local") ? 0 : 1;
}

int RunMinimalApplicationModel(ae::RamDomainStorage& user_storage,
                               ae::RamDomainStorage& local_storage,
                               std::filesystem::path const& state_dir,
                               ApplicationModelRuntime& runtime,
                               ApplicationPublishedCallback on_published,
                               ui::UiDeviceEnvironment const* startup_environment) {
  if (!LoadUserStorageFile(user_storage, state_dir) ||
      !LoadUserStorageFile(local_storage, state_dir / "local")) return 1;
  ApplicationModelSessionHooks<MinimalApplication> hooks;
  hooks.prepare = [startup_environment](MinimalApplication& application) {
    PrepareMinimalApplicationForRun(application);
    if (startup_environment) application.main_window->device_state->ObserveEnvironment(
        *startup_environment, ui::UiEnvironmentReason::kStartup);
  };
  hooks.persist = [&user_storage, &local_storage, &state_dir](MinimalApplication& application) -> int {
    return PersistAndroidMinimalUser(*application.domain, user_storage, local_storage, state_dir);
  };

  return RunEmbeddedApplicationModelSession(
      user_storage, local_storage, embedded_distilled::minimal::kEmbeddedGraph, runtime,
      std::move(on_published), hooks);
}

}  // namespace apptraverse::example::minimal
