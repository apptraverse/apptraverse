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

constexpr ae::ObjId::Type kOriginPRaw = 91010u;
constexpr ae::ObjId::Type kOriginQRaw = 91011u;
constexpr ae::ObjId::Type kOriginCRaw = 91012u;
constexpr ae::ObjId::Type kRootRRaw = 91013u;
constexpr ae::ObjId::Type kChildDRaw = 91014u;
constexpr ae::ObjId::Type kCloneRRaw = 91015u;

constexpr std::uint32_t kSavedValue = 150u;
constexpr std::uint32_t kSavedTag = 251u;
constexpr std::uint32_t kMemoryValue = 1601u;
constexpr std::uint32_t kMemoryTag = 252u;

constexpr std::uint32_t kGraphSavedRootValue = 301u;
constexpr std::uint32_t kGraphSavedChildValue = 302u;
constexpr std::uint32_t kGraphSavedConstValue = 303u;
constexpr std::uint32_t kGraphMemoryRootValue = 3601u;
constexpr std::uint32_t kGraphMemoryChildValue = 3602u;
constexpr std::uint32_t kGraphMemoryConstTag = 362u;

ae::ObjId const kPrefabId{kPrefabIdRaw};
ae::ObjId const kCloneId{kCloneIdRaw};
ae::ObjId const kConflictId{kConflictIdRaw};
ae::ObjId const kChildId{kChildIdRaw};
ae::ObjId const kOriginP{kOriginPRaw};
ae::ObjId const kOriginQ{kOriginQRaw};
ae::ObjId const kOriginC{kOriginCRaw};
ae::ObjId const kRootR{kRootRRaw};
ae::ObjId const kChildD{kChildDRaw};
ae::ObjId const kCloneR{kCloneRRaw};

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

  AE_OBJECT_REFLECT(AE_MMBR(value), AE_MMBR(child), AE_MMBR(shared_const),
                    AE_MMBR(child_id_snapshot))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, value, child, shared_const, child_id_snapshot);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, value, child, shared_const, child_id_snapshot);
  }

  std::uint32_t value{0};
  ae::ObjPtr<CloneFacetTargetV1> child{};
  ae::ObjPtr<CloneFacetTargetV1> shared_const{};
  ae::ObjId child_id_snapshot{};
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

DiskStores OpenDiskStores(std::filesystem::path const& state_root,
                          std::set<ae::ObjId> const& overlay_constants) {
  auto user = std::make_unique<apptraverse::DirectoryDomainStorage>(
      state_root / "user");
  auto backing = std::make_unique<apptraverse::DirectoryDomainStorage>(
      state_root / "backing");
  auto overlay = std::make_unique<apptraverse::OverlayDomainStorage>(
      *user, *backing, overlay_constants);
  return DiskStores{std::move(user), std::move(backing), std::move(overlay)};
}

void SeedGraphConstantsOnBacking(std::filesystem::path const& state_root) {
  std::filesystem::create_directories(state_root / "backing");
  apptraverse::DirectoryDomainStorage backing{state_root / "backing"};
  ae::Domain domain{backing};
  CloneFacetTargetV1::ptr p = CloneFacetTargetV1::ptr::Create(
      ae::CreateWith{domain}.with_id(kOriginP));
  ae::Ptr<CloneFacetTargetV1> p_body = p.Load();
  p_body->value = kGraphSavedRootValue;
  p_body->tag = kGraphSavedConstValue;
  CloneFacetTargetV1::ptr q = CloneFacetTargetV1::ptr::Create(
      ae::CreateWith{domain}.with_id(kOriginQ));
  ae::Ptr<CloneFacetTargetV1> q_body = q.Load();
  q_body->value = kGraphSavedChildValue;
  q_body->tag = kGraphSavedConstValue + 1u;
  CloneFacetTargetV1::ptr c = CloneFacetTargetV1::ptr::Create(
      ae::CreateWith{domain}.with_id(kOriginC));
  ae::Ptr<CloneFacetTargetV1> c_body = c.Load();
  c_body->value = kGraphSavedConstValue;
  c_body->tag = kGraphMemoryConstTag;
  ae::DomainGraph graph{&domain};
  backing.BeginSaveSession();
  if (!graph.SaveRoot(p_body, kOriginP).IsOk() ||
      !graph.SaveRoot(q_body, kOriginQ).IsOk() ||
      !graph.SaveRoot(c_body, kOriginC).IsOk()) {
    Fail("SaveRoot P/Q/C on BACKING");
  }
  if (backing.SaveSessionHadFailure()) {
    Fail("backing save session failed for graph constants");
  }
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

  DiskStores stores = OpenDiskStores(state_root, {kPrefabId});
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
  if (!cloned || cloned.new_root_id() != kCloneId) {
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
    DiskStores cold = OpenDiskStores(state_root, {kPrefabId});
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

void TestCanonicalCloneTwoObjectGraphWithConstBoundary() {
  std::filesystem::path const state_root =
      std::filesystem::temp_directory_path() /
      "apptraverse_object_clone_graph_test";
  std::filesystem::remove_all(state_root);
  std::filesystem::create_directories(state_root / "source" / "user");
  std::filesystem::create_directories(state_root / "target" / "user");
  SeedGraphConstantsOnBacking(state_root);

  std::set<ae::ObjId> const overlay_constants{kOriginP, kOriginQ, kOriginC};
  DiskFileSnapshot const backing_before =
      SnapshotDiskObjectTree(state_root / "backing", kOriginC);

  {
    DiskStores source_stores =
        OpenDiskStores(state_root / "source", overlay_constants);
    ae::Domain source_domain{*source_stores.overlay};
    CloneFacetTargetV1::ptr child = CloneFacetTargetV1::ptr::Create(
        ae::CreateWith{source_domain}.with_id(kChildD));
    ae::Ptr<CloneFacetTargetV1> child_body = child.Load();
    child_body->value = kGraphSavedChildValue;
    child_body->tag = kGraphSavedConstValue + 2u;
    child_body->SetOriginId(kOriginQ);

    CloneGraphTargetV1::ptr root = CloneGraphTargetV1::ptr::Create(
        ae::CreateWith{source_domain}.with_id(kRootR));
    ae::Ptr<CloneGraphTargetV1> root_body = root.Load();
    root_body->value = kGraphSavedRootValue;
    root_body->child = child;
    root_body->shared_const = CloneFacetTargetV1::ptr::Declare(
        ae::CreateWith{&source_domain}
            .with_id(kOriginC)
            .with_flags(ae::ObjFlags::kUnloadedByDefault |
                        ae::ObjFlags::kUnloaded));
    root_body->child_id_snapshot = kChildD;
    root_body->SetOriginId(kOriginP);

    source_stores.user->BeginSaveSession();
    ae::DomainGraph source_save{&source_domain};
    if (!source_save.SaveRoot(root_body, kRootR).IsOk() ||
        !source_save.SaveRoot(child_body, kChildD).IsOk()) {
      Fail("SaveRoot R/D on source USER");
    }
    if (source_stores.user->SaveSessionHadFailure()) {
      Fail("source USER save session failed");
    }

    DiskFileSnapshot const source_r_before =
        SnapshotDiskObjectTree(state_root / "source" / "user", kRootR);
    DiskFileSnapshot const source_d_before =
        SnapshotDiskObjectTree(state_root / "source" / "user", kChildD);

    ae::DomainGraph source_load{&source_domain};
    ae::Ptr<ae::Obj> loaded_r;
    if (!source_load.LoadRoot(kRootR, loaded_r).IsOk()) {
      Fail("LoadRoot R before clone");
    }
    ae::Ptr<CloneGraphTargetV1> mutable_r{loaded_r};
    mutable_r->value = kGraphMemoryRootValue;
    mutable_r->child.Load()->value = kGraphMemoryChildValue;
    mutable_r->child_id_snapshot = kChildD;

    DiskStores target_stores =
        OpenDiskStores(state_root / "target", overlay_constants);
    ae::Domain target_domain{*target_stores.overlay};

    apptraverse::CloneOptions options{};
    options.stable_origin = kOriginP;
    options.boundary_ids = {kOriginP, kOriginQ, kOriginC};
    options.new_root_id = kCloneR;

    target_stores.user->BeginSaveSession();
    apptraverse::CloneCanonicalResult const cloned =
        apptraverse::CloneCanonicalRoot(source_domain, *mutable_r, target_domain,
                                        options);
    if (!cloned || cloned.new_root_id() != kCloneR || !cloned.root.is_valid()) {
      Fail("CloneCanonicalRoot R/D graph");
    }
    if (target_stores.user->SaveSessionHadFailure()) {
      Fail("target USER save session failed after clone");
    }

    ae::Ptr<CloneGraphTargetV1> clone_r{ae::Ptr<ae::Obj>{cloned.root.Load()}};
    if (!clone_r || clone_r->value != kGraphMemoryRootValue) {
      Fail("clone R value");
    }
    if (!clone_r->child.is_valid() || clone_r->child.id() == kChildD) {
      Fail("clone R child id");
    }
    if (clone_r->child.id() == kCloneR) {
      Fail("clone R child must differ from clone root id");
    }
    ae::Ptr<CloneFacetTargetV1> clone_d{clone_r->child.Load()};
    if (!clone_d || clone_d->value != kGraphMemoryChildValue) {
      Fail("clone D value");
    }
    if (!clone_r->shared_const.is_valid() ||
        clone_r->shared_const.id() != kOriginC) {
      Fail("clone R shared_const id");
    }
    if (clone_r->child_id_snapshot != kChildD) {
      Fail("scalar child_id_snapshot must stay source D id");
    }
    if (clone_r->origin_id() != kOriginP || clone_d->origin_id() != kOriginQ) {
      Fail("clone origins P/Q");
    }

    if (DiskObjectHasAnyLayers(state_root / "target" / "user", kOriginP) ||
        DiskObjectHasAnyLayers(state_root / "target" / "user", kOriginQ) ||
        DiskObjectHasAnyLayers(state_root / "target" / "user", kOriginC)) {
      Fail("target USER must not contain P/Q/C");
    }

    DiskFileSnapshot const source_r_after =
        SnapshotDiskObjectTree(state_root / "source" / "user", kRootR);
    DiskFileSnapshot const source_d_after =
        SnapshotDiskObjectTree(state_root / "source" / "user", kChildD);
    if (source_r_after != source_r_before || source_d_after != source_d_before) {
      Fail("source USER R/D changed after clone");
    }

    DiskFileSnapshot const backing_after =
        SnapshotDiskObjectTree(state_root / "backing", kOriginC);
    if (backing_after != backing_before) {
      Fail("BACKING C changed");
    }

    {
      DiskStores cold = OpenDiskStores(state_root / "target", overlay_constants);
      ae::Domain cold_domain{*cold.overlay};
      ae::DomainGraph cold_graph{&cold_domain};
      ae::Ptr<ae::Obj> cold_root;
      if (!cold_graph.LoadRoot(kCloneR, cold_root).IsOk()) {
        Fail("cold LoadRoot clone R");
      }
      ae::Ptr<CloneGraphTargetV1> cold_r{cold_root};
      ae::Ptr<CloneFacetTargetV1> cold_d{cold_r->child.Load()};
      if (cold_r->value != kGraphMemoryRootValue ||
          cold_d->value != kGraphMemoryChildValue ||
          cold_r->origin_id() != kOriginP || cold_d->origin_id() != kOriginQ ||
          cold_r->shared_const.id() != kOriginC ||
          cold_r->child_id_snapshot != kChildD) {
        Fail("cold clone graph fields");
      }
      if (cold_domain.Find(kOriginP) || cold_domain.Find(kOriginQ) ||
          cold_domain.Find(kOriginC)) {
        Fail("P/Q/C materialized during cold load");
      }
    }
  }

  std::filesystem::remove_all(state_root);
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

}  // namespace

int main() {
  apptraverse::EnsureObjectRegistration();
  (void)&g_apptraverse_registrar_CloneFacetTargetV1;
  (void)&g_apptraverse_registrar_CloneGraphTargetV1;

  TestCanonicalCloneSingleObjectMemoryRoundtrip();
  TestCanonicalCloneTwoObjectGraphWithConstBoundary();
  TestCloneMissingStableOrigin();
  TestCloneObjIdConflict();

  std::cout << "object_clone_test OK\n";
  return 0;
}
