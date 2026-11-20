#include "window_catalog_android_lifecycle.h"

#include "window_catalog_android_embedded_profile.h"
#include "window_catalog_android_persistence.h"
#include "window_catalog_prepare.h"
#include "window_catalog_menu.h"
#include "apptraverse/application_model_session.h"
#include "apptraverse/embedded_application_model_session.h"
#include "embedded_distilled_window_catalog_android_supply.h"

namespace apptraverse::example::surfaces {
int RunWindowCatalogAndroidModel(WindowCatalogAndroidEmbeddedProfile& profile,
                                 ApplicationModelRuntime& runtime,
                                 ApplicationPublishedCallback on_published,
                                 ui::UiDeviceEnvironment const& startup_environment) {
  ApplicationModelSessionHooks<apptraverse::WindowCatalog> hooks;
  hooks.prepare = [&startup_environment](apptraverse::WindowCatalog& catalog) {
    catalog.persisted.device_state.Load();
    catalog.persisted.device_state->ObserveEnvironment(
        startup_environment, ui::UiEnvironmentReason::kStartup);
    PrepareWindowCatalogForRun(catalog);
    catalog.EnableLocalWindowLifecycle();
    PrepareCatalogMenuForRun(catalog);
  };
  hooks.before_incremental = [](apptraverse::WindowCatalog& catalog) {
    // Page visibility is derived from the published order, not a focus sidecar.
    catalog.pending_focus_window = {};
  };
  hooks.persist = [&profile](apptraverse::WindowCatalog& catalog) {
    return PersistWindowCatalogEmbeddedUserOverlayFromDomain(*catalog.domain,
                                                             profile);
  };
  return RunEmbeddedApplicationModelSession(
      profile.user, profile.local, embedded_distilled::window_catalog::kEmbeddedGraph, runtime,
      std::move(on_published), hooks);
}
}  // namespace apptraverse::example::surfaces
