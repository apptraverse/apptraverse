#include <filesystem>
#include <iostream>
#include <set>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/distilled_domain_export.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/ui/ui_presenters.h"

#include "window_catalog_graph_web.h"
#include "window_catalog_ids.h"
#include "window_catalog_model.h"
#include "window_catalog_supply_constant_ids.h"

namespace {

constexpr char kHeaderGuard[] =
    "APPTRAVERSE_EMBEDDED_DISTILLED_WINDOW_CATALOG_WEB_SUPPLY_H_";
constexpr char kGraphNamespace[] = "window_catalog_web";

ae::ObjId CatalogRootId() {
  return ae::ObjId{
      apptraverse::window_catalog_demo::ToObjId(
          apptraverse::window_catalog_demo::ObjId::Catalog)};
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: apptraverse_export_window_catalog_distilled_graph_web "
                 "<output.h>\n";
    return 2;
  }

  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureWebWindowCatalogGraphRegistration();

  std::filesystem::path const export_dir =
      std::filesystem::path{argv[1]}.concat(".export_dir");
  std::error_code remove_ec;
  std::filesystem::remove_all(export_dir, remove_ec);

  std::vector<ae::ObjId> supply_constant_ids;
  {
    apptraverse::DirectoryDomainStorage storage{export_dir};
    ae::Domain domain{storage};
    auto catalog = apptraverse::BuildWebWindowCatalogGraph(domain);
    apptraverse::FinalizeDistilledGraph(*catalog);
    apptraverse::SaveDistilledRoot(*catalog);
    supply_constant_ids =
        apptraverse::CollectWindowCatalogSupplyConstantIds(*catalog);
  }

  ae::RamDomainStorage export_storage;
  if (!apptraverse::LoadDirectoryDomainStorageIntoRam(export_dir,
                                                      export_storage)) {
    std::cerr << "failed to load distilled window catalog web layers\n";
    return 1;
  }
  std::set<ae::ObjId> const object_filter =
      apptraverse::CollectStoredObjectIds(export_storage);
  std::filesystem::remove_all(export_dir, remove_ec);

  for (auto const prefab_id :
       {apptraverse::window_catalog_demo::ObjId::WindowPrefab,
        apptraverse::window_catalog_demo::ObjId::WindowPrefabPresenter,
        apptraverse::window_catalog_demo::ObjId::WindowPrefabCloseButton,
        apptraverse::window_catalog_demo::ObjId::
            WindowPrefabCloseButtonPresenter}) {
    supply_constant_ids.push_back(
        ae::ObjId{apptraverse::window_catalog_demo::ToObjId(prefab_id)});
  }

  if (!apptraverse::WriteEmbeddedDistilledSupplyHeader(
          argv[1], kGraphNamespace, kHeaderGuard, CatalogRootId(),
          supply_constant_ids, export_storage, object_filter)) {
    std::cerr << "failed to write embedded window catalog web supply\n";
    return 1;
  }
  return 0;
}
