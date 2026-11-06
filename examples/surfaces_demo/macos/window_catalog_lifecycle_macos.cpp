#include "window_catalog_lifecycle_macos.h"

#include "window_catalog_prepare.h"

#include <filesystem>
#include <iostream>

#include "window_catalog_graph_macos.h"
#include "window_catalog_ids.h"
#include "window_catalog_model.h"

#include "apptraverse/application_model_session.h"
#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/embedded_application_model_session.h"
#include "embedded_distilled_window_catalog_supply.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/ui/ui_presented_window_lifecycle.h"

namespace apptraverse {

namespace {

ae::ObjId CatalogRootId() {
  return ae::ObjId{
      window_catalog_demo::ToObjId(window_catalog_demo::ObjId::Catalog)};
}

}  // namespace

int RunMacWindowCatalogModel(
    std::filesystem::path const& state_dir, ApplicationModelRuntime& runtime,
    ApplicationPublishedCallback on_published,
    ae::ObjId* pending_focus_for_ui) {
  EnsureObjectRegistration();
  EnsureWindowCatalogGraphRegistration();

  bool const user_profile_present =
      std::filesystem::exists(state_dir) &&
      !std::filesystem::is_empty(state_dir);
  std::cerr << (user_profile_present
                    ? "WINDOW_CATALOG_MACOS_LOADED_FROM_PROFILE\n"
                    : "WINDOW_CATALOG_MACOS_LOADED_FROM_EMBEDDED_BACKING\n");

  ApplicationModelSessionHooks<WindowCatalog> hooks;
  hooks.prepare = PrepareWindowCatalogForRun;
  hooks.before_incremental = [pending_focus_for_ui](WindowCatalog& catalog) {
    if (pending_focus_for_ui != nullptr) {
      *pending_focus_for_ui = catalog.pending_focus_window.is_valid()
                                  ? catalog.pending_focus_window.id()
                                  : ae::ObjId{};
    }
    catalog.pending_focus_window = {};
  };

  DirectoryDomainStorage user_storage{state_dir};
  hooks.persist = [&user_storage](WindowCatalog& catalog) -> int {
    user_storage.BeginSaveSession();
    SaveWindowCatalogPersistentSnapshot(catalog);
    if (user_storage.SaveSessionHadFailure()) {
      WriteFatalStderr("fatal: WindowCatalog shutdown save failed\n");
      return 1;
    }
    return 0;
  };
  return RunEmbeddedApplicationModelSession(
      user_storage, embedded_distilled::window_catalog::kEmbeddedGraph, runtime,
      std::move(on_published), hooks);
}

}  // namespace apptraverse
