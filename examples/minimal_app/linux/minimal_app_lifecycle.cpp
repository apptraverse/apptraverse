#include "minimal_app_lifecycle.h"

#include <iostream>
#include <utility>

#include "apptraverse/application_model_session.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/platform/linux/linux_ui_registration.h"
#include "apptraverse/single_file_user_storage.h"
#include "apptraverse/ui/ui_presenters.h"
#ifdef APPTRAVERSE_DEBUG_FRESH_START
#include "apptraverse/distill.h"
#include "apptraverse/overlay_domain_storage.h"
#include "minimal_app_graph_linux.h"
#else
#include "apptraverse/embedded_application_model_session.h"
#include "embedded_distilled_minimal_app_supply.h"
#endif
#include "minimal_app_persistence.h"
#include "minimal_app_prepare.h"
#include "minimal_application.h"

namespace apptraverse::example::minimal {

int RunMinimalApplicationModel(
    std::filesystem::path const& state_dir,
    ApplicationModelRuntime& runtime,
    ApplicationPublishedCallback on_published) {
  EnsureObjectRegistration();
  ui::EnsureUiPresenterRegistration();
  ui::linux::EnsureLinuxUiPresenterRegistration();
  EnsureMinimalApplicationRegistration();

  ae::RamDomainStorage user;
#ifdef APPTRAVERSE_DEBUG_FRESH_START
  ae::RamDomainStorage backing;
  {
    ae::Domain producer{backing};
    auto application = BuildLinuxMinimalApplicationGraph(producer);
    FinalizeDistilledGraph(*application);
    SaveDistilledRoot(*application);
  }
  OverlayDomainStorage overlay{user, backing, {}};
#else
  if (!LoadUserStorageFile(user, state_dir)) return 1;
  std::error_code error;
  bool const has_user = std::filesystem::exists(state_dir / "user.bin", error);
  if (error) {
    WriteFatalStderr("fatal: cannot inspect minimal app USER file\n");
    std::abort();
  }
#endif
#ifdef APPTRAVERSE_DEBUG_FRESH_START
  std::cerr << "MINIMAL_APP_FRESH_DEBUG_GRAPH\n";
#else
  std::cerr << (has_user ? "MINIMAL_APP_LOADED_FROM_PROFILE\n"
                         : "MINIMAL_APP_LOADED_FROM_EMBEDDED_BACKING\n");
#endif

  ApplicationModelSessionHooks<MinimalApplication> hooks;
  hooks.prepare = PrepareMinimalApplicationForRun;
  hooks.persist = [&user, &state_dir](MinimalApplication& application) {
    int const saved = SaveMinimalApplicationEmbeddedUserState(application);
    if (saved != 0) return saved;
    return SaveUserStorageFile(user, state_dir) ? 0 : 1;
  };
#ifdef APPTRAVERSE_DEBUG_FRESH_START
  return RunApplicationModelSession(overlay, MinimalApplicationRootId(), runtime,
                                    std::move(on_published), hooks);
#else
  return RunEmbeddedApplicationModelSession(
      user, embedded_distilled::minimal::kEmbeddedGraph, runtime,
      std::move(on_published), hooks);
#endif
}

}  // namespace apptraverse::example::minimal
