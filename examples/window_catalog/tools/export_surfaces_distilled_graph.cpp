#include <cstdlib>
#include <filesystem>
#include <iostream>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj.h"

#include "apptraverse/directory_domain_storage.h"

#include "apptraverse/distill.h"
#include "apptraverse/distilled_domain_export.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/surfaces/surfaces_ids.h"
#include "apptraverse/surfaces/surfaces_registration.h"
#include "surfaces_distill.h"
#include "surfaces_supply_constant_ids.h"

namespace {

constexpr char kHeaderGuard[] =
    "APPTRAVERSE_EMBEDDED_DISTILLED_SURFACES_SUPPLY_H_";
constexpr char kGraphNamespace[] = "surfaces";

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: export_surfaces_distilled_graph <output.h>\n";
    return 2;
  }

  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureSurfacesRegistration();

  std::filesystem::path const export_dir =
      std::filesystem::temp_directory_path() /
      "apptraverse_export_surfaces_distilled_graph";
  std::error_code remove_ec;
  std::filesystem::remove_all(export_dir, remove_ec);

  apptraverse::DirectoryDomainStorage storage{export_dir};
  ae::Domain domain{storage};
  apptraverse::Application::ptr application =
      apptraverse::BuildSurfacesGraph(domain);
  apptraverse::FinalizeDistilledGraph(*application);
  apptraverse::StabilizeDistilledPersistentSnapshot(*application);
  apptraverse::SaveDistilledRoot(*application);
  ae::Ptr<apptraverse::Application> root_object = application.Load();
  ae::ObjId const root_id{root_object->obj_id};
  ae::RamDomainStorage export_storage;
  if (!apptraverse::LoadDirectoryDomainStorageIntoRam(export_dir,
                                                    export_storage)) {
    std::cerr << "failed to load distilled surfaces layers from export dir\n";
    return 1;
  }
  std::set<ae::ObjId> const object_filter =
      apptraverse::CollectStoredObjectIds(export_storage);
  std::filesystem::remove_all(export_dir, remove_ec);

  std::vector<ae::ObjId> const supply_ids =
      apptraverse::CollectSurfacesDemoSupplyConstantIds(*application);

  if (!apptraverse::WriteEmbeddedDistilledSupplyHeader(
          argv[1], kGraphNamespace, kHeaderGuard, root_id, supply_ids,
          export_storage, object_filter)) {
    std::cerr << "failed to write embedded supply header\n";
    return 1;
  }
  return 0;
}
