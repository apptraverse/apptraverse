#include "window_catalog_android_lifecycle.h"

#include "window_catalog_android_embedded_profile.h"
#include "window_catalog_graph_android.h"
#include "window_catalog_ids.h"
#include "window_catalog_prepare.h"

#include "android_log.h"

#include "apptraverse/application_model_session.h"
#include "apptraverse/embedded_application_model_session.h"
#include "apptraverse/object_macros.h"

#include "embedded_distilled_window_catalog_android_supply.h"

namespace apptraverse::example::surfaces {
namespace {

bool UserProfilePresent(std::filesystem::path const& user_root) {
  return std::filesystem::exists(user_root) &&
         !std::filesystem::is_empty(user_root);
}

}  // namespace

int RunWindowCatalogAndroidModel(WindowCatalogAndroidEmbeddedProfile& profile,
                                 ApplicationModelRuntime& runtime,
                                 ApplicationPublishedCallback on_published) {
  EnsureObjectRegistration();
  EnsureWindowCatalogAndroidGraphRegistration();

  bool const user_profile_present = UserProfilePresent(profile.user.root());
  if (user_profile_present) {
    apptraverse::android::LogInfo("WINDOW_CATALOG_LOADED_FROM_PROFILE");
  } else {
    apptraverse::android::LogInfo("WINDOW_CATALOG_LOADED_FROM_EMBEDDED_BACKING");
  }

  ApplicationModelSessionHooks<apptraverse::WindowCatalog> hooks;
  hooks.prepare = [](apptraverse::WindowCatalog& catalog) {
    PrepareWindowCatalogForRun(catalog);
  };
  hooks.persist = [&profile](apptraverse::WindowCatalog& catalog) -> int {
    profile.user.BeginSaveSession();
    SaveWindowCatalogPersistentSnapshot(catalog);
    if (profile.user.SaveSessionHadFailure()) {
      WriteFatalStderr("fatal: WindowCatalog Android persist failed\n");
      return 1;
    }
    return 0;
  };

  return RunEmbeddedApplicationModelSession(
      profile.overlay, embedded_distilled::window_catalog::kEmbeddedGraph,
      runtime,
      std::move(on_published), hooks);
}

}  // namespace apptraverse::example::surfaces
