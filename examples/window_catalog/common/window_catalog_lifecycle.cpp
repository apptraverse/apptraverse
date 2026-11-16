#include "window_catalog_lifecycle.h"

#include "window_catalog_prepare.h"

#include <filesystem>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include "window_catalog_graph.h"
#include "window_catalog_ids.h"
#include "window_catalog_localization.h"
#include "window_catalog_model.h"

#include "apptraverse/application_model_session.h"
#include "apptraverse/single_file_user_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/embedded_application_model_session.h"
#include "apptraverse/distilled_domain_export.h"
#if defined(APPTRAVERSE_WINDOW_CATALOG_USE_EMBEDDED_DISTILL)
#include "embedded_distilled_window_catalog_supply.h"
#endif
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/node.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_presented_window_lifecycle.h"
#if defined(APPTRAVERSE_WINDOW_CATALOG_DEBUG_BOOTSTRAP)
#include "window_catalog_supply_constant_ids.h"
#endif

namespace apptraverse {

namespace {

ae::ObjId CatalogRootId() {
  return ae::ObjId{
      window_catalog_demo::ToObjId(window_catalog_demo::ObjId::Catalog)};
}

}  // namespace

int WindowCatalogModelSession::Run(
    ApplicationPublishedCallback on_published) {
  main_native_frame_queue.BindPostWork(
      [this](ModelObjectProxy::ModelWork work) {
        runtime.Post(std::move(work));
      });
  ApplicationModelSessionHooks<WindowCatalog> hooks;
  hooks.before_incremental = [this](WindowCatalog& catalog) {
    pending_focus_for_ui = catalog.pending_focus_window.is_valid()
                               ? catalog.pending_focus_window.id()
                               : ae::ObjId{};
    catalog.pending_focus_window = {};

  };

#if defined(APPTRAVERSE_WINDOW_CATALOG_DEBUG_BOOTSTRAP)
  // Ordinary Debug constructs the current supply in RAM. Only lifecycle
  // checkpointing writes USER; explicit --distill-only owns supply export.
  state_dir /= "debug-run-" + std::to_string(ae::ObjId::GenerateUnique().id());
  state_dir /= "user";
  ae::RamDomainStorage backing;
  std::set<ae::ObjId> constant_ids;
  {
    ae::Domain producer_domain{backing};
    auto catalog = BuildWindowCatalogGraph(producer_domain);
    FinalizeDistilledGraph(*catalog);
    SaveDistilledRoot(*catalog);
    auto constants = CollectWindowCatalogSupplyConstantIds(*catalog);
    constant_ids.insert(constants.begin(), constants.end());
  }
  ae::RamDomainStorage user_storage;
  OverlayDomainStorage overlay{user_storage, backing, constant_ids};
  hooks.prepare = PrepareWindowCatalogForRun;
  hooks.persist = [&user_storage, state_dir = state_dir](WindowCatalog& catalog) -> int {
    SaveWindowCatalogPersistentSnapshot(catalog);
    if (!SaveUserStorageFile(user_storage, state_dir)) {
      WriteFatalStderr("fatal: WindowCatalog shutdown save failed\n");
      return 1;
    }
    return 0;
  };
  return RunApplicationModelSession(overlay, CatalogRootId(), runtime,
                                    std::move(on_published), hooks);
#elif defined(APPTRAVERSE_WINDOW_CATALOG_USE_EMBEDDED_DISTILL)
  ae::RamDomainStorage user_storage;
  if (!LoadUserStorageFile(user_storage, state_dir)) {
    WriteFatalStderr("fatal: cannot load catalog USER file\n");
    return 1;
  }
  hooks.prepare = [](WindowCatalog& catalog) {
    PrepareWindowCatalogForRun(catalog);
  };
  hooks.persist = [&user_storage, state_dir = state_dir](WindowCatalog& catalog) -> int {
    SaveWindowCatalogPersistentSnapshot(catalog);
    if (!SaveUserStorageFile(user_storage, state_dir)) {
      WriteFatalStderr("fatal: WindowCatalog shutdown save failed\n");
      return 1;
    }
    return 0;
  };
  return RunEmbeddedApplicationModelSession(
      user_storage, embedded_distilled::window_catalog::kEmbeddedGraph, runtime,
      std::move(on_published), hooks);
#else
#error "WindowCatalog requires fresh Debug producer or current embedded supply"

#endif
}

}  // namespace apptraverse
