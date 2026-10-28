#include "window_catalog_lifecycle.h"

#include "window_catalog_prepare.h"

#include <filesystem>
#include <optional>
#include <unordered_set>
#include <vector>

#include "window_catalog_graph.h"
#include "window_catalog_ids.h"
#include "window_catalog_localization.h"
#include "window_catalog_model.h"

#include "apptraverse/application_model_session.h"
#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/embedded_application_model_session.h"
#if defined(APPTRAVERSE_ENABLE_DISTILLATION)
#include "apptraverse/distilled_domain_export.h"
#endif
#if defined(APPTRAVERSE_WINDOW_CATALOG_USE_EMBEDDED_DISTILL)
#include "window_catalog_distilled.h"
#endif
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/node.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_presented_window_lifecycle.h"

namespace apptraverse {

namespace {

ae::ObjId CatalogRootId() {
  return ae::ObjId{
      window_catalog_demo::ToObjId(window_catalog_demo::ObjId::Catalog)};
}

}  // namespace

int WindowCatalogModelSession::Run(
    ApplicationPublishedCallback on_published) {
  EnsureObjectRegistration();
  EnsureWindowCatalogGraphRegistration();

  ApplicationModelSessionHooks<WindowCatalog> hooks;
  hooks.prepare = PrepareWindowCatalogForRun;
  hooks.before_incremental = [this](WindowCatalog& catalog) {
    pending_focus_for_ui = catalog.pending_focus_window.is_valid()
                               ? catalog.pending_focus_window.id()
                               : ae::ObjId{};
    catalog.pending_focus_window = {};
  };

#if defined(APPTRAVERSE_WINDOW_CATALOG_USE_EMBEDDED_DISTILL)
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
#else
#if defined(APPTRAVERSE_ENABLE_DISTILLATION)
  std::error_code profile_error;
  bool const profile_exists = std::filesystem::exists(state_dir, profile_error);
  if (profile_error) {
    WriteFatalStderr("fatal: cannot inspect catalog profile\n");
    std::abort();
  }
  bool const profile_absent = !profile_exists ||
      std::filesystem::is_empty(state_dir, profile_error);
  if (profile_error) {
    WriteFatalStderr("fatal: cannot inspect catalog profile contents\n");
    std::abort();
  }
  if (profile_absent) {
    DirectoryDomainStorage bootstrap_storage{state_dir};
    ae::Domain bootstrap_domain{bootstrap_storage};
    auto bootstrap_catalog = BuildWindowCatalogGraph(bootstrap_domain);
    FinalizeDistilledGraph(*bootstrap_catalog);
    SaveDistilledRoot(*bootstrap_catalog);
  }
#endif

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
  return RunApplicationModelSession(user_storage, CatalogRootId(), runtime,
                                    std::move(on_published), hooks);
#endif
}

}  // namespace apptraverse
