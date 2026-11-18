#include "minimal_app_lifecycle_macos.h"

#include <utility>
#include "apptraverse/application_model_session.h"
#include "apptraverse/embedded_application_model_session.h"
#include "apptraverse/single_file_user_storage.h"
#if defined(APPTRAVERSE_MINIMAL_APPLE_DEBUG_FRESH)
#include "apptraverse/distill.h"
#include "minimal_app_graph_macos.h"
#else
#include "embedded_distilled_minimal_app_macos_supply.h"
#endif
#include "minimal_app_persistence.h"
#include "minimal_app_prepare.h"
#include "minimal_application.h"

namespace apptraverse::example::minimal {

int RunMacMinimalApplicationModel(
    std::filesystem::path const& state_dir,
    ApplicationModelRuntime& runtime,
    ApplicationPublishedCallback on_published,
    ui::UiDeviceEnvironment const& environment) {
  ae::RamDomainStorage user, local;
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
    int const saved = SaveMinimalApplicationEmbeddedUserState(application);
    if (saved != 0) return saved;
    return SaveUserStorageFile(user, state_dir) &&
                   SaveUserStorageFile(local, state_dir / "local") ? 0 : 1;
  };
#if defined(APPTRAVERSE_MINIMAL_APPLE_DEBUG_FRESH)
  ae::RamDomainStorage backing;
  {
    ae::Domain build_domain{backing};
    auto application = BuildMacMinimalApplicationGraph(build_domain);
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
