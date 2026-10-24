#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

#include "apptraverse/distilled_supply_package.h"
#include "apptraverse/distilled_domain_export.h"
#include "apptraverse/object_macros.h"
#include "prefab_release_ids.h"
#include "prefab_release_model.h"

#ifndef APPTRAVERSE_PREFAB_PACKAGE_BASE_RELEASE
#define APPTRAVERSE_PREFAB_PACKAGE_BASE_RELEASE 0
#endif
#ifndef APPTRAVERSE_PREFAB_PACKAGE_TARGET_RELEASE
#define APPTRAVERSE_PREFAB_PACKAGE_TARGET_RELEASE 1
#endif

int main(int argc, char** argv) {
  if (argc < 2 || argc > 4) {
    std::cerr << "usage: export_prefab_release_r1_package <supply.bin> "
                 "[--negatives <dir>]\n";
    return 2;
  }
  std::filesystem::path negatives_dir;
  if (argc == 4) {
    if (std::string_view{argv[2]} != "--negatives") {
      std::cerr << "usage: export_prefab_release_r1_package <supply.bin> "
                   "[--negatives <dir>]\n";
      return 2;
    }
    negatives_dir = argv[3];
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
  apptraverse::DistilledSupplyPackageError error{};
  if (!apptraverse::BuildDistilledSupplyPackageFromStorageDiff(
          base_storage, target_storage, object_filter, supply_ids,
          APPTRAVERSE_PREFAB_PACKAGE_BASE_RELEASE,
          APPTRAVERSE_PREFAB_PACKAGE_TARGET_RELEASE, package, &error)) {
    std::cerr << "package build failed: "
              << (error.message ? error.message : "?") << "\n";
    return 1;
  }

  if (package.operations.size() != 1) {
    std::cerr << "expected exactly one layer op, got "
              << package.operations.size() << "\n";
    return 1;
  }

  if (!apptraverse::WriteDistilledSupplyPackageFile(argv[1], package, &error)) {
    std::cerr << "package write failed: "
              << (error.message ? error.message : "?") << "\n";
    return 1;
  }

  if (!negatives_dir.empty()) {
    std::filesystem::create_directories(negatives_dir);
    apptraverse::DistilledSupplyPackage duplicate = package;
    duplicate.operations.push_back(duplicate.operations.front());
    if (!apptraverse::WriteDistilledSupplyPackageFile(
            negatives_dir / "duplicate_layer_key.bin", duplicate, &error,
            false)) {
      std::cerr << "duplicate negative write failed\n";
      return 1;
    }
    apptraverse::DistilledSupplyPackage dynamic_op = package;
    apptraverse::DistilledSupplyPackageOperation workspace{};
    workspace.key = {apptraverse::test::prefab_release::kWorkspaceId, 1u, 0u};
    workspace.op = apptraverse::DistilledSupplyPackageOp::kUpsert;
    workspace.bytes = {0x01};
    dynamic_op.operations.push_back(std::move(workspace));
    if (!apptraverse::WriteDistilledSupplyPackageFile(
            negatives_dir / "dynamic_workspace_obj.bin", dynamic_op, &error,
            false)) {
      std::cerr << "workspace negative write failed\n";
      return 1;
    }
  }
  return 0;
}
