#include <cstdlib>
#include <iostream>

#include "apptraverse/distilled_domain_export.h"
#include "apptraverse/object_macros.h"
#include "prefab_release_ids.h"
#include "prefab_release_model.h"

namespace {

constexpr char kHeaderGuard[] =
    "APPTRAVERSE_EMBEDDED_PREFAB_RELEASE_SUPPLY_H_";
constexpr char kGraphNamespace[] = "prefab_release";

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: export_prefab_release_backing <output.h>\n";
    return 2;
  }

  apptraverse::EnsureObjectRegistration();
  apptraverse::test::prefab_release::EnsurePrefabReleaseRegistration();

  ae::RamDomainStorage const storage =
      apptraverse::test::prefab_release::BuildPrefabSupplyGraph();
  std::set<ae::ObjId> const object_filter =
      apptraverse::CollectStoredObjectIds(storage);
  std::vector<ae::ObjId> const supply_ids = {
      ae::ObjId{apptraverse::test::prefab_release::kMainLabelConstId},
      ae::ObjId{apptraverse::test::prefab_release::kToolLabelConstId}};

  ae::ObjId const root_id{
      apptraverse::test::prefab_release::kMainWindowPrefabId};

  if (!apptraverse::WriteEmbeddedDistilledSupplyHeader(
          argv[1], kGraphNamespace, kHeaderGuard, root_id, supply_ids, storage,
          object_filter)) {
    std::cerr << "failed to write prefab release embedded header\n";
    return 1;
  }
  return 0;
}
