#include "minimal_app_lifecycle.h"

#include "minimal_application.h"

#include <filesystem>

#if defined(APPTRAVERSE_ENABLE_DISTILLATION) && \
    !defined(APPTRAVERSE_MINIMAL_APP_LOAD_ONLY) && \
    !defined(APPTRAVERSE_MINIMAL_APP_USE_EMBEDDED_DISTILL)
#include "minimal_app_graph.h"
#endif

#include "apptraverse/application_model_session.h"
#include "apptraverse/single_file_user_storage.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/distill.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"

#if defined(APPTRAVERSE_MINIMAL_APP_USE_EMBEDDED_DISTILL)
#include "apptraverse/embedded_application_model_session.h"
#include "embedded_distilled_minimal_app_supply.h"
#endif

#include "minimal_app_persistence.h"
#include "minimal_app_prepare.h"

#include "apptraverse/ui/ui_presented_window_lifecycle.h"
#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::example::minimal {
namespace {

bool UserProfilePresent(std::filesystem::path const& state_dir) {
  std::error_code profile_error;
  bool const profile_exists = std::filesystem::exists(state_dir / "user.bin", profile_error);
  if (profile_error) {
    WriteFatalStderr("fatal: cannot inspect minimal app profile\n");
    std::abort();
  }
  return profile_exists;
}

ApplicationModelSessionHooks<MinimalApplication> MakeSessionHooks(
    SingleFileUserStorage& durable_storage, bool normalize_loaded_user) {
  ApplicationModelSessionHooks<MinimalApplication> hooks;
  hooks.prepare = [normalize_loaded_user](MinimalApplication& application) {
    PrepareMinimalApplicationForRun(application);
    if (normalize_loaded_user) {
      NormalizeMinimalApplicationAfterEmbeddedUserLoad(application);
    }
  };
  hooks.persist = [&durable_storage](MinimalApplication& application) -> int {
    int const result = SaveMinimalApplicationEmbeddedUserState(application);
    return result != 0 || !durable_storage.SaveFile() ? 1 : 0;
  };
  return hooks;
}

}  // namespace

int RunMinimalApplicationModel(
    std::filesystem::path const& state_dir,
    ApplicationModelRuntime& runtime,
    ApplicationPublishedCallback on_published,
    bool allow_distilled_bootstrap) {
  EnsureObjectRegistration();
  ui::EnsureUiPresenterRegistration();
  ui::windows::EnsureWinUiPresenterRegistration();
  EnsureMinimalApplicationRegistration();

  SingleFileUserStorage durable_storage{state_dir};
  if (!durable_storage.LoadFile()) {
    WriteFatalStderr("fatal: cannot load MinimalApp USER file\n");
    return 1;
  }
  bool const user_profile_present = UserProfilePresent(state_dir);

#if defined(APPTRAVERSE_MINIMAL_APP_USE_EMBEDDED_DISTILL)
  ApplicationModelSessionHooks<MinimalApplication> hooks =
      MakeSessionHooks(durable_storage, user_profile_present);
  (void)allow_distilled_bootstrap;
  return RunEmbeddedApplicationModelSession(
      durable_storage, embedded_distilled::minimal::kEmbeddedGraph, runtime,
      std::move(on_published), hooks);
#else
  ApplicationModelSessionHooks<MinimalApplication> hooks =
      MakeSessionHooks(durable_storage, false);
#if defined(APPTRAVERSE_ENABLE_DISTILLATION) && \
    !defined(APPTRAVERSE_MINIMAL_APP_LOAD_ONLY)
  if (allow_distilled_bootstrap) {
    if (durable_storage.state.empty()) {
      ae::Domain domain{durable_storage};
      auto application = BuildMinimalApplicationGraph(domain);
      FinalizeDistilledGraph(*application);
      SaveDistilledRoot(*application);
    }
  }
#else
  (void)allow_distilled_bootstrap;
#endif
  return RunApplicationModelSession(
      durable_storage, MinimalApplicationRootId(), runtime,
      std::move(on_published), hooks);
#endif
}

}  // namespace apptraverse::example::minimal
