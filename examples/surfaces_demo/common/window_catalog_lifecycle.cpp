#include "window_catalog_lifecycle.h"

#include "window_catalog_prepare.h"

#include <filesystem>
#include <optional>
#include <vector>

#include "window_catalog_ids.h"
#include "window_catalog_model.h"

#include "apptraverse/application_model_session.h"
#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/noninteractive_crt.h"

namespace apptraverse {

namespace {

ae::ObjId CatalogRootId() {
  return ae::ObjId{
      window_catalog_demo::ToObjId(window_catalog_demo::ObjId::Catalog)};
}

int RunWindowCatalogModelSessionWithStorage(
    DirectoryDomainStorage& user_storage, ApplicationModelRuntime& runtime,
    ApplicationPublishedCallback on_published,
    ApplicationModelSessionHooks<WindowCatalog>& hooks) {
  return RunApplicationModelSession(user_storage, CatalogRootId(), runtime,
                                    std::move(on_published), hooks);
}

}  // namespace

int WindowCatalogModelSession::Run(
    ApplicationPublishedCallback on_published) {
  EnsureObjectRegistration();
  EnsureWindowCatalogModelRegistration();

  ApplicationModelSessionHooks<WindowCatalog> hooks;
  hooks.prepare = PrepareWindowCatalogForRun;
  hooks.before_incremental = [this](WindowCatalog& catalog) {
    pending_focus_for_ui = catalog.pending_focus_window.is_valid()
                               ? catalog.pending_focus_window.id()
                               : ae::ObjId{};
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
  return RunWindowCatalogModelSessionWithStorage(user_storage, runtime,
                                                 std::move(on_published),
                                                 hooks);
}

}  // namespace apptraverse
