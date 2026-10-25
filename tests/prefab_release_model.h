#ifndef APPTRAVERSE_TESTS_PREFAB_RELEASE_MODEL_H_
#define APPTRAVERSE_TESTS_PREFAB_RELEASE_MODEL_H_

#include <cstdint>
#include <optional>

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

void EnsurePrefabReleaseRegistration();

PrefabDefaults MainWindowPrefabDefaults();
PrefabDefaults ToolWindowPrefabDefaults();

ae::RamDomainStorage BuildPrefabSupplyGraph();
ae::RamDomainStorage BuildPrefabSupplyGraphWithMainPosx(std::int32_t main_posx);
ae::RamDomainStorage BuildPrefabSupplyGraphWithMainTitleAndPosx(
    char const* main_title, std::int32_t main_posx);

void StripPrefabWindowS1FromSupplyGraph(ae::RamDomainStorage& storage);

ae::ObjPtr<ae::Obj> CloneWorkspaceFromPrefabs(ae::Domain& domain,
                                              bool create_window_c);

std::uint32_t LazyLoadCount();
void ResetLazyLoadCount();

class PrefabWindow;
class WorkspaceRoot;

struct PrefabBackedLoadContext {
  ae::Domain& domain;
  ae::RamDomainStorage const& backing;
  DirectoryDomainStorage const& user;
};

struct CreatedToolWindowInstance {
  PrefabPtr<PrefabWindow> canonical;
  ae::ObjId instance_id;
  ae::ObjId instance_base_id;
  std::uint32_t width{0};
};

std::optional<std::uint32_t> ReadPrefabWidthFromBacking(
    ae::RamDomainStorage const& backing, ae::ObjId prefab_id);

CreatedToolWindowInstance CreateToolWindowInstanceFromBacking(
    ae::Domain& domain, ae::RamDomainStorage const& backing);

ae::Ptr<PrefabWindow> MaterializePrefabBackedWindow(
    PrefabPtr<PrefabWindow> const& canonical, PrefabBackedLoadContext const& ctx);

bool ValidateWorkspaceCanonicalPrefabPtrs(WorkspaceRoot const& ws,
                                          char const** error_out);

// Fixture-only: materialize historical base S1 from prefab-base before first S1 Event.
bool EnsurePrefabVersionInHistoricalBase(
    PrefabPtr<PrefabWindow> const& canonical, PrefabWindow& instance,
    ae::Domain& domain, DirectoryDomainStorage& user,
    ae::RamDomainStorage const& backing, std::uint8_t version);

int RunPrefabReleaseFixture(int argc, char** argv);

}  // namespace apptraverse::test::prefab_release

#endif  // APPTRAVERSE_TESTS_PREFAB_RELEASE_MODEL_H_
