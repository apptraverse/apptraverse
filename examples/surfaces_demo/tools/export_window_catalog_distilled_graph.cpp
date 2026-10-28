#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>

#include "aether-objects/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/distill.h"
#include "apptraverse/distilled_domain_export.h"
#include "apptraverse/object_macros.h"

#include "window_catalog_graph.h"
#include "window_catalog_ids.h"
#include "window_catalog_model.h"

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
    std::cerr << "usage: apptraverse_export_window_catalog_distilled_graph "
                 "<output.h>\n";
    return 2;
  }

  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureWindowCatalogGraphRegistration();

  std::filesystem::path const export_dir =
      std::filesystem::temp_directory_path() /
      "apptraverse_export_window_catalog_distilled_graph";
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

  if (!apptraverse::WriteEmbeddedDistilledSupplyHeader(
          argv[1], kGraphNamespace, kHeaderGuard, CatalogRootId(), {},
          export_storage, object_filter)) {
    std::cerr << "failed to write embedded window catalog supply header\n";
    return 1;
  }
  return 0;
}
