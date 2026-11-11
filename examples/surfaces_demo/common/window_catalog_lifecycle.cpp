#include "window_catalog_lifecycle.h"

#include "window_catalog_prepare.h"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "window_catalog_graph.h"
#include "window_catalog_ids.h"
#include "window_catalog_localization.h"
#include "window_catalog_model.h"

#include "apptraverse/application_model_session.h"
#include "apptraverse/directory_domain_storage.h"
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
#include "window_catalog_distillation.h"
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
  std::error_code directory_error;
  std::filesystem::create_directories(state_dir, directory_error);
  if (directory_error) {
    WriteFatalStderr("fatal: cannot create catalog development directory\n");
    std::abort();
  }
  std::filesystem::path run_directory;
  do {
    run_directory = state_dir /
        ("debug-run-" + std::to_string(ae::ObjId::GenerateUnique().id()));
    if (std::filesystem::create_directory(run_directory, directory_error)) {
      break;
    }
    if (directory_error) {
      WriteFatalStderr("fatal: cannot create catalog debug-run directory\n");
      std::abort();
    }
  } while (true);
  auto const initial = DistillWindowCatalog(run_directory);
  state_dir = run_directory / "user";
  std::cerr << "WINDOW_CATALOG_DEBUG_FRESH_PROFILE=" << state_dir.string()
            << '\n';
  ae::RamDomainStorage backing;
  if (!LoadDirectoryDomainStorageIntoRam(initial.backing_directory, backing)) {
    WriteFatalStderr("fatal: cannot load catalog initial backing\n");
    return 1;
  }
  ae::RamDomainStorage user_storage;
  OverlayDomainStorage overlay{user_storage, backing, initial.constant_ids};
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
  ae::RamDomainStorage user_storage;
  if (!LoadUserStorageFile(user_storage, state_dir)) return 1;
#if defined(APPTRAVERSE_ENABLE_DISTILLATION)
  if (user_storage.state.empty()) {
    ae::Domain bootstrap_domain{user_storage};
    auto bootstrap_catalog = BuildWindowCatalogGraph(bootstrap_domain);
    FinalizeDistilledGraph(*bootstrap_catalog);
    SaveDistilledRoot(*bootstrap_catalog);
  }
#endif
  hooks.prepare = PrepareWindowCatalogForRun;
  hooks.persist = [&user_storage, state_dir = state_dir](WindowCatalog& catalog) -> int {
    SaveWindowCatalogPersistentSnapshot(catalog);
    return SaveUserStorageFile(user_storage, state_dir) ? 0 : 1;
  };
  return RunApplicationModelSession(user_storage, CatalogRootId(), runtime,
                                    std::move(on_published), hooks);
#endif
}

}  // namespace apptraverse
