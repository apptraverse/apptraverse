#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/object_clone.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/overlay_domain_storage.h"

namespace {

constexpr ae::ObjId::Type kPrefabIdRaw = 91001u;
constexpr ae::ObjId::Type kCloneIdRaw = 91002u;
constexpr ae::ObjId::Type kConflictIdRaw = 91003u;
constexpr ae::ObjId::Type kChildIdRaw = 91004u;

constexpr std::uint32_t kSavedValue = 150u;
constexpr std::uint32_t kSavedTag = 251u;
constexpr std::uint32_t kMemoryValue = 1601u;
constexpr std::uint32_t kMemoryTag = 252u;

ae::ObjId const kPrefabId{kPrefabIdRaw};
ae::ObjId const kCloneId{kCloneIdRaw};
ae::ObjId const kConflictId{kConflictIdRaw};
ae::ObjId const kChildId{kChildIdRaw};

using StoredLayerSet =
    std::map<std::uint32_t, std::set<std::uint8_t>>;

void Fail(char const* message) {
  std::cerr << "object_clone_test FAIL: " << message << '\n';
  std::exit(1);
}

class CloneFacetTargetV1 : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::CloneFacetTargetV1",
                           CloneFacetTargetV1, ae::Obj, 0)

 protected:
  CloneFacetTargetV1() = default;

 public:
  explicit CloneFacetTargetV1(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(value), AE_MMBR(tag))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, value);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, value);
  }

  template <typename Dnv>
  void Load(ae::Version<1>, Dnv& dnv) {
    dnv(base_, tag);
  }

  template <typename Dnv>
  void Save(ae::Version<1>, Dnv& dnv) const {
    dnv(base_, tag);
  }

  std::uint32_t value{0};
  std::uint32_t tag{0};
};

class CloneGraphTargetV1 : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::CloneGraphTargetV1",
                           CloneGraphTargetV1, ae::Obj, 1)

 protected:
  CloneGraphTargetV1() = default;

 public:
  explicit CloneGraphTargetV1(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(value), AE_MMBR(child))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, value, child);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, value, child);
  }

  std::uint32_t value{0};
  ae::ObjPtr<CloneFacetTargetV1> child{};
};

APPTRAVERSE_REGISTER(CloneFacetTargetV1);
APPTRAVERSE_REGISTER(CloneGraphTargetV1);

StoredLayerSet CollectDiskLayers(std::filesystem::path const& storage_root,
                                 ae::ObjId object_id) {
  StoredLayerSet layers;
  auto const obj_dir = storage_root / std::to_string(object_id.id());
  if (!std::filesystem::exists(obj_dir)) {
    return layers;
  }
  for (auto const& class_dir : std::filesystem::directory_iterator(obj_dir)) {
    if (!class_dir.is_directory()) {
      continue;
    }
    std::uint32_t const class_id = static_cast<std::uint32_t>(
        std::stoul(class_dir.path().filename().string()));
    for (auto const& version_file :
         std::filesystem::directory_iterator(class_dir.path())) {
      if (!version_file.is_regular_file()) {
        continue;
      }
      std::uint8_t const version = static_cast<std::uint8_t>(
          std::stoul(version_file.path().filename().string()));
      layers[class_id].insert(version);
    }
  }
  return layers;
}

bool DiskObjectHasAnyLayers(std::filesystem::path const& storage_root,
                            ae::ObjId object_id) {
  return !CollectDiskLayers(storage_root, object_id).empty();
}

using DiskFileSnapshot =
    std::map<std::string, std::vector<std::uint8_t>>;

DiskFileSnapshot SnapshotDiskObjectTree(std::filesystem::path const& storage_root,
                                        ae::ObjId object_id) {
  DiskFileSnapshot snapshot;
  auto const obj_dir = storage_root / std::to_string(object_id.id());
  if (!std::filesystem::exists(obj_dir)) {
    return snapshot;
  }
  for (auto const& entry :
       std::filesystem::recursive_directory_iterator(obj_dir)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    auto const rel =
        std::filesystem::relative(entry.path(), obj_dir).generic_string();
    std::ifstream in{entry.path(), std::ios::binary | std::ios::ate};
    if (!in) {
      Fail("failed to read disk layer file");
    }
    auto const size = static_cast<std::size_t>(in.tellg());
    in.seekg(0);
    std::vector<std::uint8_t> bytes(size);
    if (size != 0) {
      in.read(reinterpret_cast<char*>(bytes.data()),
              static_cast<std::streamsize>(size));
    }
    snapshot[rel] = std::move(bytes);
  }
  return snapshot;
}

void ExpectCloneUserLayers(StoredLayerSet const& layers) {
  if (layers.size() != 2u) {
    Fail("USER/A unexpected ClassId count");
  }
  if (layers.at(CloneFacetTargetV1::kClassId) !=
      StoredLayerSet::mapped_type{0, 1}) {
    Fail("USER/A unexpected Derived S layers");
  }
  if (layers.at(ae::Obj::kClassId) != std::set<std::uint8_t>{0, 1}) {
    Fail("USER/A unexpected Obj S layers");
  }
}

struct DiskStores {
  std::unique_ptr<apptraverse::DirectoryDomainStorage> user;
  std::unique_ptr<apptraverse::DirectoryDomainStorage> backing;
  std::unique_ptr<apptraverse::OverlayDomainStorage> overlay;
};

DiskStores OpenDiskStores(std::filesystem::path const& state_root) {
  auto user = std::make_unique<apptraverse::DirectoryDomainStorage>(
      state_root / "user");
  auto backing = std::make_unique<apptraverse::DirectoryDomainStorage>(
      state_root / "backing");
  auto overlay = std::make_unique<apptraverse::OverlayDomainStorage>(
      *user, *backing, std::set<ae::ObjId>{kPrefabId});
  return DiskStores{std::move(user), std::move(backing), std::move(overlay)};
}

void SeedPrefabOnBacking(std::filesystem::path const& state_root) {
  std::filesystem::create_directories(state_root / "backing");
  std::filesystem::create_directories(state_root / "user");
  apptraverse::DirectoryDomainStorage backing{state_root / "backing"};
  ae::Domain domain{backing};
  CloneFacetTargetV1::ptr prefab = CloneFacetTargetV1::ptr::Create(
      ae::CreateWith{domain}.with_id(kPrefabId));
  ae::Ptr<CloneFacetTargetV1> body = prefab.Load();
  body->value = kSavedValue;
  body->tag = kSavedTag;
  ae::DomainGraph graph{&domain};
  backing.BeginSaveSession();
  if (!graph.SaveRoot(body, kPrefabId).IsOk()) {
    Fail("SaveRoot P on BACKING");
  }
  if (backing.SaveSessionHadFailure()) {
    Fail("backing save session failed");
  }
}

void TestCanonicalCloneSingleObjectMemoryRoundtrip() {
  std::filesystem::path const state_root =
      std::filesystem::temp_directory_path() / "apptraverse_object_clone_test";
  std::filesystem::remove_all(state_root);
  SeedPrefabOnBacking(state_root);

  DiskFileSnapshot const backing_p_before =
      SnapshotDiskObjectTree(state_root / "backing", kPrefabId);

  DiskStores stores = OpenDiskStores(state_root);
  ae::Domain domain{*stores.overlay};
  ae::DomainGraph load_graph{&domain};
  ae::Ptr<ae::Obj> prefab_root;
  if (!load_graph.LoadRoot(kPrefabId, prefab_root).IsOk()) {
    Fail("LoadRoot P");
  }
  ae::Ptr<CloneFacetTargetV1> prefab{prefab_root};
  prefab->value = kMemoryValue;
  prefab->tag = kMemoryTag;

  apptraverse::CloneOptions options{};
  options.stable_origin = kPrefabId;
  options.boundary_ids = {kPrefabId};
  options.new_root_id = kCloneId;

  stores.user->BeginSaveSession();
  apptraverse::CloneCanonicalResult const cloned =
      apptraverse::CloneCanonicalRoot(domain, *prefab, domain, options);
  if (!cloned || cloned.new_root_id != kCloneId) {
    Fail("CloneCanonicalRoot P to A");
  }
  if (stores.user->SaveSessionHadFailure()) {
    Fail("USER save session failed after clone");
  }

  ae::DomainGraph verify_graph{&domain};
  ae::Ptr<ae::Obj> instance_root;
  if (!verify_graph.LoadRoot(kCloneId, instance_root).IsOk()) {
    Fail("LoadRoot A after clone");
  }
  ae::Ptr<CloneFacetTargetV1> instance{instance_root};
  if (!instance) {
    Fail("A ClassId mismatch after clone");
  }
  if (instance->value != kMemoryValue) {
    std::cerr << "value=" << instance->value << " expected=" << kMemoryValue
              << '\n';
    Fail("A value after clone");
  }
  if (instance->tag != kMemoryTag) {
    std::cerr << "tag=" << instance->tag << " expected=" << kMemoryTag << '\n';
    Fail("A tag after clone");
  }
  if (instance->origin_id() != kPrefabId) {
    Fail("A origin after clone");
  }

  ExpectCloneUserLayers(
      CollectDiskLayers(state_root / "user", kCloneId));
  if (DiskObjectHasAnyLayers(state_root / "user", kPrefabId)) {
    Fail("USER must not contain P layers");
  }

  DiskFileSnapshot const backing_p_after =
      SnapshotDiskObjectTree(state_root / "backing", kPrefabId);
  if (backing_p_after != backing_p_before) {
    Fail("BACKING P changed");
  }

  {
    DiskStores cold = OpenDiskStores(state_root);
    ae::Domain cold_domain{*cold.overlay};
    ae::DomainGraph cold_graph{&cold_domain};
    ae::Ptr<ae::Obj> cold_root;
    if (!cold_graph.LoadRoot(kCloneId, cold_root).IsOk()) {
      Fail("cold LoadRoot A");
    }
    ae::Ptr<CloneFacetTargetV1> cold_a{cold_root};
    if (cold_a->value != kMemoryValue || cold_a->tag != kMemoryTag ||
        cold_a->origin_id() != kPrefabId) {
      Fail("cold A fields or origin");
    }
    if (cold_domain.Find(kPrefabId)) {
      Fail("P materialized during cold LoadRoot(A)");
    }
  }

  std::filesystem::remove_all(state_root);
}

void TestCloneMissingStableOrigin() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  CloneFacetTargetV1::ptr object = CloneFacetTargetV1::ptr::Create(
      ae::CreateWith{domain}.with_id(kPrefabId));
  object.Load()->value = 1;
  apptraverse::CloneOptions options{};
  options.stable_origin = ae::ObjId{0u};
  auto const result = apptraverse::CloneCanonicalRoot(
      domain, *object.Load(), domain, options);
  if (result || result.error != apptraverse::CloneError::kMissingStableOrigin) {
    Fail("expected MissingStableOrigin");
  }
}

void TestCloneObjIdConflict() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  CloneFacetTargetV1::ptr existing = CloneFacetTargetV1::ptr::Create(
      ae::CreateWith{domain}.with_id(kConflictId));
  existing.Load()->value = 1;
  CloneFacetTargetV1::ptr source = CloneFacetTargetV1::ptr::Create(
      ae::CreateWith{domain}.with_id(kPrefabId));
  source.Load()->value = 2;
  apptraverse::CloneOptions options{};
  options.stable_origin = kPrefabId;
  options.new_root_id = kConflictId;
  auto const result = apptraverse::CloneCanonicalRoot(
      domain, *source.Load(), domain, options);
  if (result || result.error != apptraverse::CloneError::kObjIdConflict) {
    Fail("expected ObjIdConflict");
  }
}

void TestCloneUnsupportedGraph() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  CloneFacetTargetV1::ptr child = CloneFacetTargetV1::ptr::Create(
      ae::CreateWith{domain}.with_id(kChildId));
  child.Load()->value = 9;
  CloneGraphTargetV1::ptr root = CloneGraphTargetV1::ptr::Create(
      ae::CreateWith{domain}.with_id(kPrefabId));
  ae::Ptr<CloneGraphTargetV1> body = root.Load();
  body->value = 3;
  body->child = child;
  apptraverse::CloneOptions options{};
  options.stable_origin = kPrefabId;
  options.new_root_id = kCloneId;
  auto const result = apptraverse::CloneCanonicalRoot(
      domain, *body, domain, options);
  if (result || result.error != apptraverse::CloneError::kUnsupportedGraph) {
    Fail("expected UnsupportedGraph");
  }
}

}  // namespace

int main() {
  apptraverse::EnsureObjectRegistration();
  (void)&g_apptraverse_registrar_CloneFacetTargetV1;
  (void)&g_apptraverse_registrar_CloneGraphTargetV1;

  TestCanonicalCloneSingleObjectMemoryRoundtrip();
  TestCloneMissingStableOrigin();
  TestCloneObjIdConflict();
  TestCloneUnsupportedGraph();

  std::cout << "object_clone_test OK\n";
  return 0;
}
