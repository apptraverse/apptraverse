#include "window_catalog_distillation.h"

#include <cstdlib>
#include <string>
#include <vector>

#include "aether-objects/obj/domain.h"
#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/distilled_domain_export.h"
#include "apptraverse/object_macros.h"
#include "window_catalog_graph.h"
#include "window_catalog_ids.h"
#include "window_catalog_model.h"
#include "window_catalog_supply_constant_ids.h"

namespace apptraverse {

WindowCatalogInitialSupply DistillWindowCatalog(
    std::filesystem::path const& output_directory) {
  EnsureObjectRegistration();
  EnsureWindowCatalogGraphRegistration();
  WindowCatalogInitialSupply supply;
  // Never overwrite a previous development run or a user's existing profile.
  for (;;) {
    supply.backing_directory = output_directory /
        ("initial-" + std::to_string(ae::ObjId::GenerateUnique().id()));
    std::error_code error;
    std::filesystem::create_directories(output_directory, error);
    if (!error && std::filesystem::create_directory(supply.backing_directory,
                                                   error)) {
      break;
    }
    if (error) {
      WriteFatalStderr("fatal: cannot create catalog initial-state directory\n");
      std::abort();
    }
  }
  DirectoryDomainStorage storage{supply.backing_directory};
  storage.BeginSaveSession();
  ae::Domain domain{storage};
  auto catalog = BuildWindowCatalogGraph(domain);
  FinalizeDistilledGraph(*catalog);
  SaveDistilledRoot(*catalog);
  if (storage.SaveSessionHadFailure()) {
    WriteFatalStderr("fatal: catalog initial-state save failed\n");
    std::abort();
  }

  ae::RamDomainStorage exported;
  if (!LoadDirectoryDomainStorageIntoRam(supply.backing_directory, exported)) {
    WriteFatalStderr("fatal: catalog initial-state read failed\n");
    std::abort();
  }
  auto constants = CollectWindowCatalogSupplyConstantIds(*catalog);
  using window_catalog_demo::ObjId;
  using window_catalog_demo::ToObjId;
  constants.push_back(ae::ObjId{ToObjId(ObjId::WindowPrefab)});
  constants.push_back(ae::ObjId{ToObjId(ObjId::WindowPrefabPresenter)});
  supply.constant_ids.insert(constants.begin(), constants.end());
  if (!WriteEmbeddedDistilledSupplyHeader(
          output_directory / "embedded_distilled_window_catalog_supply.h",
          "window_catalog",
          "APPTRAVERSE_EMBEDDED_DISTILLED_WINDOW_CATALOG_SUPPLY_H_",
          ae::ObjId{ToObjId(ObjId::Catalog)}, constants, exported,
          CollectStoredObjectIds(exported))) {
    WriteFatalStderr("fatal: catalog embedded-header write failed\n");
    std::abort();
  }
  return supply;
}

}  // namespace apptraverse
