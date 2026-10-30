#include "window_catalog_lifecycle.h"

#include "window_catalog_prepare.h"

#include <filesystem>

#include "window_catalog_graph.h"
#include "window_catalog_ids.h"
#include "window_catalog_model.h"

#include "apptraverse/application_model_session.h"
#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/embedded_application_model_session.h"
#include "apptraverse/noninteractive_crt.h"
#if defined(APPTRAVERSE_WINDOW_CATALOG_USE_EMBEDDED_DISTILL)
#include "window_catalog_distilled.h"
#endif

namespace apptraverse {

namespace {

ae::ObjId CatalogRootId() {
  return ae::ObjId{
      window_catalog_demo::ToObjId(window_catalog_demo::ObjId::Catalog)};
}

}  // namespace

#if defined(APPTRAVERSE_WINDOW_CATALOG_USE_EMBEDDED_DISTILL)

int RunWindowCatalogEmbeddedModelSession(
    WindowCatalogModelSession& session,
    ApplicationPublishedCallback on_published) {
  EnsureObjectRegistration();
  EnsureWindowCatalogGraphRegistration();

  ApplicationModelSessionHooks<WindowCatalog> hooks;
  hooks.prepare = PrepareWindowCatalogForRun;
  hooks.before_incremental = [&session](WindowCatalog& catalog) {
    session.pending_focus_for_ui = catalog.pending_focus_window.is_valid()
                                       ? catalog.pending_focus_window.id()
                                       : ae::ObjId{};
    catalog.pending_focus_window = {};
  };

  DirectoryDomainStorage user_storage{session.state_dir};
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
      user_storage, embedded_distilled::window_catalog::kEmbeddedGraph,
      session.runtime, std::move(on_published), hooks);
}

#endif  // APPTRAVERSE_WINDOW_CATALOG_USE_EMBEDDED_DISTILL

#if defined(APPTRAVERSE_ENABLE_DISTILLATION)

void BootstrapWindowCatalogProfileIfAbsent(std::filesystem::path const& state_dir) {
  std::error_code profile_error;
  bool const profile_exists = std::filesystem::exists(state_dir, profile_error);
  if (profile_error) {
    WriteFatalStderr("fatal: cannot inspect catalog profile\n");
    std::abort();
  }
  bool const profile_absent =
      !profile_exists || std::filesystem::is_empty(state_dir, profile_error);
  if (profile_error) {
    WriteFatalStderr("fatal: cannot inspect catalog profile contents\n");
    std::abort();
  }
  if (!profile_absent) {
    return;
  }
  DirectoryDomainStorage bootstrap_storage{state_dir};
  ae::Domain bootstrap_domain{bootstrap_storage};
  auto bootstrap_catalog = BuildWindowCatalogGraph(bootstrap_domain);
  FinalizeDistilledGraph(*bootstrap_catalog);
  SaveDistilledRoot(*bootstrap_catalog);
}

#endif

}  // namespace apptraverse
