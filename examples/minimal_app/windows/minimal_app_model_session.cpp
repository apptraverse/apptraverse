#include "minimal_app_lifecycle.h"
#include "minimal_application.h"
#include "minimal_app_persistence.h"
#include "minimal_app_prepare.h"
#include "apptraverse/application_model_session.h"
#include "apptraverse/single_file_user_storage.h"
#if defined(APPTRAVERSE_MINIMAL_APP_DEBUG_BOOTSTRAP)
#include "minimal_app_graph.h"
#include "apptraverse/distill.h"
#include "apptraverse/overlay_domain_storage.h"
#else
#include "apptraverse/embedded_application_model_session.h"
#include "embedded_distilled_minimal_app_supply.h"
#endif

namespace apptraverse::example::minimal {
int RunMinimalApplicationModel(std::filesystem::path const& requested_state_dir,
    ApplicationModelRuntime& runtime, ApplicationPublishedCallback on_published) {
  auto state_dir = requested_state_dir;
  ae::RamDomainStorage user;
#if defined(APPTRAVERSE_MINIMAL_APP_DEBUG_BOOTSTRAP)
  auto const run_directory = state_dir /
      ("debug-run-" + std::to_string(ae::ObjId::GenerateUnique().id()));
  state_dir = run_directory / "user";
  ae::RamDomainStorage backing;
  {
    ae::Domain producer{backing};
    auto application = BuildMinimalApplicationGraph(producer);
    FinalizeDistilledGraph(*application);
    SaveDistilledRoot(*application);
  }
  OverlayDomainStorage overlay{user, backing, {}};
#else
  if (!LoadUserStorageFile(user, state_dir)) { return 1; }
#endif
  ApplicationModelSessionHooks<MinimalApplication> hooks;
  hooks.prepare = PrepareMinimalApplicationForRun;
  hooks.persist = [&user, state_dir](MinimalApplication& application) {
    int const result = SaveMinimalApplicationEmbeddedUserState(application);
    return result != 0 || !SaveUserStorageFile(user, state_dir) ? 1 : 0;
  };
#if defined(APPTRAVERSE_MINIMAL_APP_DEBUG_BOOTSTRAP)
  return RunApplicationModelSession(overlay, MinimalApplicationRootId(), runtime,
                                    std::move(on_published), hooks);
#else
  return RunEmbeddedApplicationModelSession(user, embedded_distilled::minimal::kEmbeddedGraph,
                                            runtime, std::move(on_published), hooks);
#endif
}
}
