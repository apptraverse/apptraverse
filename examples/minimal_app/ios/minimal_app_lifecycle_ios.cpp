#include "minimal_app_lifecycle_ios.h"

#include <utility>
#include "apptraverse/application_model_session.h"
#include "apptraverse/single_file_user_storage.h"
#if defined(APPTRAVERSE_MINIMAL_APPLE_DEBUG_FRESH)
#include "apptraverse/distill.h"
#include "apptraverse/overlay_domain_storage.h"
#include "minimal_app_graph_ios.h"
#else
#include "apptraverse/embedded_application_model_session.h"
#include "embedded_distilled_minimal_app_ios_supply.h"
#endif
#include "minimal_app_persistence.h"
#include "minimal_app_prepare.h"
#include "minimal_application.h"

namespace apptraverse::example::minimal {

int RunIosMinimalApplicationModel(
    std::filesystem::path const& state_dir, ae::RamDomainStorage& user,
    ApplicationModelRuntime& runtime,
    ApplicationPublishedCallback on_published) {
#ifndef APPTRAVERSE_MINIMAL_APPLE_DEBUG_FRESH
  if (!LoadUserStorageFile(user, state_dir)) return 1;
#endif
  ApplicationModelSessionHooks<MinimalApplication> hooks;
  hooks.prepare = [](MinimalApplication& application) {
    PrepareMinimalApplicationForRun(application);
    // Select the native environment before publishing an imported USER. The
    // first real viewport event will replace these persisted logical dimensions.
    application.SubmitViewport(application.viewport.width,
                               application.viewport.height, 160, true);
  };
  hooks.persist = [&user, &state_dir](MinimalApplication& application) {
    int const saved = SaveMinimalApplicationEmbeddedUserState(application);
    if (saved != 0) return saved;
    return SaveUserStorageFile(user, state_dir) ? 0 : 1;
  };
#if defined(APPTRAVERSE_MINIMAL_APPLE_DEBUG_FRESH)
  ae::RamDomainStorage backing;
  {
    ae::Domain build_domain{backing};
    auto application = BuildIosMinimalApplicationGraph(build_domain);
    FinalizeDistilledGraph(*application);
    SaveDistilledRoot(*application);
  }
  OverlayDomainStorage storage{user, backing, {}};
  return RunApplicationModelSession(storage, MinimalApplicationRootId(), runtime,
                                    std::move(on_published), hooks);
#else
  return RunEmbeddedApplicationModelSession(
      user, embedded_distilled::minimal::kEmbeddedGraph, runtime,
      std::move(on_published), hooks);
#endif
}

}  // namespace apptraverse::example::minimal
