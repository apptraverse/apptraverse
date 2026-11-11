#include "window_catalog_android_lifecycle.h"

#include "window_catalog_android_embedded_profile.h"
#include "window_catalog_android_persistence.h"
#include "window_catalog_prepare.h"
#include "apptraverse/application_model_session.h"
#include "apptraverse/embedded_application_model_session.h"
#include "embedded_distilled_window_catalog_android_supply.h"

namespace apptraverse::example::surfaces {
int RunWindowCatalogAndroidModel(WindowCatalogAndroidEmbeddedProfile& profile,
                                 ApplicationModelRuntime& runtime,
                                 ApplicationPublishedCallback on_published) {
  ApplicationModelSessionHooks<apptraverse::WindowCatalog> hooks;
  hooks.prepare = PrepareWindowCatalogForRun;
  hooks.persist = [&profile](apptraverse::WindowCatalog& catalog) {
    return PersistWindowCatalogEmbeddedUserOverlayFromDomain(*catalog.domain,
                                                             profile);
  };
  return RunEmbeddedApplicationModelSession(
      profile.user, embedded_distilled::window_catalog::kEmbeddedGraph, runtime,
      std::move(on_published), hooks);
}
}  // namespace apptraverse::example::surfaces
