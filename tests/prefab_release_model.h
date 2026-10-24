#ifndef APPTRAVERSE_TESTS_PREFAB_RELEASE_MODEL_H_
#define APPTRAVERSE_TESTS_PREFAB_RELEASE_MODEL_H_

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj_id.h"

#include "apptraverse/directory_domain_storage.h"
#include "prefab_ptr.h"
#include "prefab_release_ids.h"

namespace apptraverse::test::prefab_release {

struct PrefabDefaults {
  std::uint32_t width{};
  std::int32_t posx{};
};

struct ClonePlan {
  std::unordered_map<ae::ObjId::Type, ae::ObjId::Type> instance_remap;
  std::vector<ae::ObjId> supply_constant_ids;
};

void EnsurePrefabReleaseRegistration();

PrefabDefaults MainWindowPrefabDefaults();
PrefabDefaults ToolWindowPrefabDefaults();

ae::RamDomainStorage BuildPrefabSupplyGraph();

ClonePlan PlanCloneFromPrefabGraph(ae::Domain const& domain);

ae::ObjPtr<ae::Obj> CloneWorkspaceFromPrefabs(ae::Domain& domain,
                                              ClonePlan const& plan,
                                              bool create_window_c);

std::uint32_t LazyLoadCount();
void ResetLazyLoadCount();

int RunPrefabReleaseFixture(int argc, char** argv);

}  // namespace apptraverse::test::prefab_release

#endif  // APPTRAVERSE_TESTS_PREFAB_RELEASE_MODEL_H_
