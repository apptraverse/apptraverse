#include "surface_ui_session.h"
#include "apptraverse/application_model_session.h"
#include "apptraverse/embedded_application_model_session.h"
#include "apptraverse/single_file_user_storage.h"
#if !defined(APPTRAVERSE_SURFACE_UI_DEBUG_BOOTSTRAP)
#include "embedded_surface_ui_supply.h"
#endif
namespace apptraverse::example::surface_ui {
int SurfaceSession::Run(ApplicationPublishedCallback published) {
  auto directory = state_dir;
  ae::RamDomainStorage user, local;
#if defined(APPTRAVERSE_SURFACE_UI_DEBUG_BOOTSTRAP)
  if (!load_only) {
    directory /= "debug-run-" + std::to_string(ae::ObjId::GenerateUnique().id());
    directory /= "user";
  }
  ae::RamDomainStorage backing;
  MakeSurfaceSupply(backing, factories,
#if defined(APPTRAVERSE_SURFACE_UI_V1)
                    true
#else
                    false
#endif
  );
  if (load_only) {
#endif
  if (!LoadUserStorageFile(user, directory) || !LoadUserStorageFile(local, directory / "local")) return 1;
  if (local.state.empty() && !local_seed_dir.empty()) {
    ae::RamDomainStorage seed;
    if (!LoadUserStorageFile(seed, local_seed_dir)) return 1;
    SeedClientLocalStorage(local, seed);
  }
#if defined(APPTRAVERSE_SURFACE_UI_DEBUG_BOOTSTRAP)
  }
#endif
  ApplicationModelSessionHooks<SurfaceApplication> hooks;
  hooks.prepare = [&](SurfaceApplication& app) {
    PrepareSurfaceUi(app, factories);
    app.stop_when_empty = [&] { runtime.RequestStop(); };
    app.resources.device->ObserveEnvironment(environment, ui::UiEnvironmentReason::kStartup);
    if (app.windows.empty()) runtime.RequestStop();
  };
  hooks.before_incremental = [&](SurfaceApplication& app) {
    pending_focus_for_ui = app.focus_request;
    app.focus_request = {};
  };
  hooks.persist = [&](SurfaceApplication& app) {
    if (SaveSurfaceUi(app) != 0) return 1;
    return SaveUserStorageFile(user, directory) && SaveUserStorageFile(local, directory / "local") ? 0 : 1;
  };
#if defined(APPTRAVERSE_SURFACE_UI_DEBUG_BOOTSTRAP)
  return RunClientLocalApplicationModelSession(user, local, backing, SurfaceSupplyConstants(backing), ApplicationId(), runtime, std::move(published), hooks);
#else
  return RunEmbeddedApplicationModelSession(user, local, embedded_distilled::surface_ui::kEmbeddedGraph, runtime, std::move(published), hooks);
#endif
}
}
