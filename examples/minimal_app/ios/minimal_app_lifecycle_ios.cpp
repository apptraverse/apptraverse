#include "minimal_app_lifecycle_ios.h"

#include <utility>
#include "apptraverse/application_model_session.h"
#include "apptraverse/embedded_application_model_session.h"
#include "apptraverse/single_file_user_storage.h"
#if defined(APPTRAVERSE_MINIMAL_APPLE_DEBUG_FRESH)
#include "apptraverse/distill.h"
#include "minimal_app_graph_ios.h"
#else
#include "embedded_distilled_minimal_app_ios_supply.h"
#endif
#include "minimal_app_persistence.h"
#include "minimal_app_prepare.h"
#include "minimal_application.h"

namespace apptraverse::example::minimal {

int PersistIosMinimalApplication(MinimalApplication& application,
    ae::RamDomainStorage& user, ae::RamDomainStorage& local,
    std::filesystem::path const& state_dir) {
  int const saved = SaveMinimalApplicationEmbeddedUserState(application);
  if (saved != 0) return saved;
  return SaveUserStorageFile(user, state_dir) &&
                 SaveUserStorageFile(local, state_dir / "local") ? 0 : 1;
}

int RunIosMinimalApplicationModel(
    std::filesystem::path const& state_dir, ae::RamDomainStorage& user,
    ae::RamDomainStorage& local, ApplicationModelRuntime& runtime,
    ApplicationPublishedCallback on_published,
    ui::UiDeviceEnvironment const& environment) {
#ifndef APPTRAVERSE_MINIMAL_APPLE_DEBUG_FRESH
  if (!LoadUserStorageFile(user, state_dir) ||
      !LoadUserStorageFile(local, state_dir / "local")) return 1;
#endif
  ApplicationModelSessionHooks<MinimalApplication> hooks;
  hooks.prepare = [&environment](MinimalApplication& application) {
    PrepareMinimalApplicationForRun(application);
    application.main_window->device_state->ObserveEnvironment(
        environment, ui::UiEnvironmentReason::kStartup);
  };
  hooks.persist = [&user, &local, &state_dir](MinimalApplication& application) {
    return PersistIosMinimalApplication(application, user, local, state_dir);
  };
#if defined(APPTRAVERSE_MINIMAL_APPLE_DEBUG_FRESH)
  ae::RamDomainStorage backing;
  {
    ae::Domain build_domain{backing};
    auto application = BuildIosMinimalApplicationGraph(build_domain);
    FinalizeDistilledGraph(*application);
    SaveDistilledRoot(*application);
  }
  return RunClientLocalApplicationModelSession(user, local, backing, {},
      MinimalApplicationRootId(), runtime, std::move(on_published), hooks);
#else
  return RunEmbeddedApplicationModelSession(
      user, local, embedded_distilled::minimal::kEmbeddedGraph, runtime,
      std::move(on_published), hooks);
#endif
}

}  // namespace apptraverse::example::minimal
