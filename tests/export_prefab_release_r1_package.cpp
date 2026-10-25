#include <cstdlib>
#include <iostream>

#include "apptraverse/distilled_supply_package.h"
#include "apptraverse/distilled_domain_export.h"
#include "apptraverse/object_macros.h"
#include "prefab_release_ids.h"
#include "prefab_release_model.h"

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: export_prefab_release_r1_package <supply.bin>\n";
    return 2;
  }

  apptraverse::EnsureObjectRegistration();
  apptraverse::test::prefab_release::EnsurePrefabReleaseRegistration();

  ae::RamDomainStorage const base_storage =
      apptraverse::test::prefab_release::BuildPrefabSupplyGraphWithMainTitleAndPosx(
          apptraverse::test::prefab_release::kMainTitleR0, 10);
  ae::RamDomainStorage const target_storage =
      apptraverse::test::prefab_release::BuildPrefabSupplyGraphWithMainTitleAndPosx(
          apptraverse::test::prefab_release::kMainTitleR1, 10);

  std::set<ae::ObjId> const object_filter =
      apptraverse::CollectStoredObjectIds(base_storage);
  std::vector<ae::ObjId> const supply_ids = {
      ae::ObjId{apptraverse::test::prefab_release::kMainLabelConstId},
      ae::ObjId{apptraverse::test::prefab_release::kToolLabelConstId}};

  apptraverse::DistilledSupplyPackage package{};
  apptraverse::BuildDistilledSupplyPackageFromStorageDiff(
      base_storage, target_storage, object_filter, supply_ids, package);

  if (!apptraverse::WriteDistilledSupplyPackageFile(argv[1], package)) {
    std::cerr << "package write failed\n";
    return 1;
  }
  return 0;
}
