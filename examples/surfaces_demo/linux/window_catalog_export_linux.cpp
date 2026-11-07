#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <set>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/distilled_domain_export.h"
#include "apptraverse/object_macros.h"

#include "window_catalog_graph.h"
#include "window_catalog_ids.h"
#include "window_catalog_model.h"
#include "window_catalog_supply_constant_ids.h"

namespace {

constexpr char kHeaderGuard[] =
    "APPTRAVERSE_EMBEDDED_DISTILLED_WINDOW_CATALOG_SUPPLY_H_";
constexpr char kGraphNamespace[] = "window_catalog";

ae::ObjId CatalogRootId() {
  return ae::ObjId{
      apptraverse::window_catalog_demo::ToObjId(
          apptraverse::window_catalog_demo::ObjId::Catalog)};
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: apptraverse_export_window_catalog_distilled_graph_linux "
                 "<output.h>\n";
    return 2;
  }

  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureWindowCatalogGraphRegistration();

  std::filesystem::path const output_path =
      std::filesystem::absolute(argv[1]);
  std::size_t const dir_tag =
      std::hash<std::string>{}(output_path.generic_string());
  std::filesystem::path const export_dir =
      std::filesystem::temp_directory_path() /
      ("apptraverse_export_window_catalog_distilled_linux_" +
       std::to_string(dir_tag));
  std::error_code remove_ec;
  std::filesystem::remove_all(export_dir, remove_ec);

  apptraverse::DirectoryDomainStorage storage{export_dir};
  ae::Domain domain{storage};
  auto catalog = apptraverse::BuildWindowCatalogGraph(domain);
  ae::Ptr<apptraverse::WindowCatalog> root = catalog.Load();
  apptraverse::FinalizeDistilledGraph(*root);
  apptraverse::SaveDistilledRoot(*root);

  ae::RamDomainStorage export_storage;
  if (!apptraverse::LoadDirectoryDomainStorageIntoRam(export_dir,
                                                      export_storage)) {
    std::cerr << "failed to load distilled window catalog layers\n";
    return 1;
  }
  std::set<ae::ObjId> const object_filter =
      apptraverse::CollectStoredObjectIds(export_storage);
  std::filesystem::remove_all(export_dir, remove_ec);

  std::vector<ae::ObjId> supply_constant_ids =
      apptraverse::CollectWindowCatalogSupplyConstantIds(*root);
  supply_constant_ids.push_back(ae::ObjId{apptraverse::window_catalog_demo::ToObjId(
      apptraverse::window_catalog_demo::ObjId::WindowPrefab)});
  supply_constant_ids.push_back(ae::ObjId{apptraverse::window_catalog_demo::ToObjId(
      apptraverse::window_catalog_demo::ObjId::WindowPrefabPresenter)});

  if (!apptraverse::WriteEmbeddedDistilledSupplyHeader(
          argv[1], kGraphNamespace, kHeaderGuard, CatalogRootId(),
          supply_constant_ids, export_storage, object_filter)) {
    std::cerr << "failed to write embedded window catalog supply header\n";
    return 1;
  }
  return 0;
}
