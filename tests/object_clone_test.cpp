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

#include "aether-objects/obj/registry.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/event_for.h"
#include "apptraverse/node.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_clone.h"
#include "apptraverse/object_link.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/overlay_domain_storage.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_locale_settings.h"
#include "apptraverse/ui/ui_localization_string.h"
#include "apptraverse/ui/ui_presented_window_composition.h"
#include "apptraverse/ui/ui_presenters.h"
#include "apptraverse/ui/ui_string.h"

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

constexpr ae::ObjId::Type kHistOriginPRaw = 92010u;
constexpr ae::ObjId::Type kHistOriginQRaw = 92011u;
constexpr ae::ObjId::Type kHistOriginCRaw = 92012u;
constexpr ae::ObjId::Type kHistRootRRaw = 92013u;
constexpr ae::ObjId::Type kHistChildDRaw = 92014u;
constexpr ae::ObjId::Type kHistCloneRRaw = 92015u;
constexpr ae::ObjId::Type kHistOriginContainerRaw = 92016u;
constexpr ae::ObjId::Type kHistContainerRootRaw = 92017u;
constexpr ae::ObjId::Type kHistCloneContainerRaw = 92018u;
constexpr ae::ObjId::Type kHistContainerChildRaw = 92019u;
constexpr ae::ObjId::Type kHistLazyRootRaw = 92020u;
constexpr ae::ObjId::Type kHistLazyChildRaw = 92021u;
constexpr ae::ObjId::Type kHistCloneLazyRootRaw = 92022u;

constexpr std::uint32_t kHistInitialRootValue = 100u;
constexpr std::uint32_t kHistInitialChildValue = 101u;
constexpr std::uint32_t kHistSavedRootValue = 200u;
constexpr std::uint32_t kHistSavedChildValue = 202u;
constexpr std::uint32_t kHistMemoryRootValue = 3601u;
constexpr std::uint32_t kHistMemoryChildValue = 3602u;
constexpr std::uint32_t kHistConstValue = 303u;
constexpr std::uint32_t kHistPostCloneCommitValue = 4001u;
constexpr std::uint32_t kHistContainerChildMemoryValue = 3701u;
constexpr std::uint32_t kHistContainerPostCloneCommitValue = 3702u;
constexpr std::uint32_t kHistLazyChildSavedValue = 2201u;
constexpr std::uint32_t kHistLazyChildMemoryValue = 3801u;
constexpr std::uint32_t kHistLazyPostCloneCommitValue = 3802u;

ae::ObjId const kHistOriginP{kHistOriginPRaw};
ae::ObjId const kHistOriginQ{kHistOriginQRaw};
ae::ObjId const kHistOriginC{kHistOriginCRaw};
ae::ObjId const kHistRootR{kHistRootRRaw};
ae::ObjId const kHistChildD{kHistChildDRaw};
ae::ObjId const kHistCloneR{kHistCloneRRaw};
ae::ObjId const kHistOriginContainer{kHistOriginContainerRaw};
ae::ObjId const kHistContainerRoot{kHistContainerRootRaw};
ae::ObjId const kHistCloneContainer{kHistCloneContainerRaw};
ae::ObjId const kHistContainerChild{kHistContainerChildRaw};
ae::ObjId const kHistLazyRoot{kHistLazyRootRaw};
ae::ObjId const kHistLazyChild{kHistLazyChildRaw};
ae::ObjId const kHistCloneLazyRoot{kHistCloneLazyRootRaw};

constexpr ae::ObjId::Type kUiPrefabWindowRaw = 93001u;
constexpr ae::ObjId::Type kUiPrefabPresenterRaw = 93002u;
constexpr ae::ObjId::Type kUiPrefabTitleRaw = 93003u;
constexpr ae::ObjId::Type kUiPrefabLocaleRaw = 93004u;
constexpr ae::ObjId::Type kUiPrefabTitleEnRaw = 93005u;
constexpr ae::ObjId::Type kUiPrefabTitleRuRaw = 93006u;
constexpr ae::ObjId::Type kUiCloneInstanceARaw = 93010u;
constexpr ae::ObjId::Type kUiCloneInstanceBRaw = 93011u;

constexpr std::int32_t kUiPrefabFrameX = 10;
constexpr std::int32_t kUiPrefabFrameY = 20;
constexpr std::int32_t kUiPrefabClientWidth = 640;
constexpr std::int32_t kUiPrefabClientHeight = 480;
constexpr std::int32_t kUiInstanceMutatedX = 55;
constexpr std::int32_t kUiInstanceMutatedY = 66;

ae::ObjId const kUiPrefabWindow{kUiPrefabWindowRaw};
ae::ObjId const kUiPrefabPresenter{kUiPrefabPresenterRaw};
ae::ObjId const kUiPrefabTitle{kUiPrefabTitleRaw};
ae::ObjId const kUiPrefabLocale{kUiPrefabLocaleRaw};
ae::ObjId const kUiPrefabTitleEn{kUiPrefabTitleEnRaw};
ae::ObjId const kUiPrefabTitleRu{kUiPrefabTitleRuRaw};
ae::ObjId const kUiCloneInstanceA{kUiCloneInstanceARaw};
ae::ObjId const kUiCloneInstanceB{kUiCloneInstanceBRaw};

ae::ObjFlags const kLazyChildFlags{ae::ObjFlags::kUnloadedByDefault |
                                   ae::ObjFlags::kUnloaded};

class CloneHistConst : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::CloneHistConst", CloneHistConst,
                           ae::Obj, 0)

 protected:
  CloneHistConst() = default;

 public:
  explicit CloneHistConst(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(value))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, value);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, value);
  }

  std::uint32_t value{};
};

class CloneHistNode;
class CloneHistSetValueEvent;

class CloneHistNode : public apptraverse::NodeFor<CloneHistNode> {
  APPTRAVERSE_OBJECT(CloneHistNode, apptraverse::Node, 0)

 protected:
  CloneHistNode() = default;

 public:
  explicit CloneHistNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(value), AE_MMBR(child), AE_MMBR(shared_const))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, value, child, shared_const);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, value, child, shared_const);
  }

  std::uint32_t value{};
  CloneHistNode::ptr child{};
  apptraverse::LocalPtr<CloneHistConst> shared_const{};

  void Apply(CloneHistSetValueEvent const& event);
};

class CloneHistSetValueEvent
    : public apptraverse::EventFor<CloneHistNode, CloneHistSetValueEvent> {
  APPTRAVERSE_OBJECT(CloneHistSetValueEvent, apptraverse::Event, 0)

 protected:
  CloneHistSetValueEvent() = default;

 public:
  explicit CloneHistSetValueEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(value))

  std::uint32_t value{};
};

void CloneHistNode::Apply(CloneHistSetValueEvent const& event) {
  value = event.value;
}

class CloneHistContainer : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::CloneHistContainer",
                           CloneHistContainer, ae::Obj, 0)

 protected:
  CloneHistContainer() = default;

 public:
  explicit CloneHistContainer(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(child))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, child);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, child);
  }

  CloneHistNode::ptr child{};
};

APPTRAVERSE_REGISTER(CloneHistConst);
APPTRAVERSE_REGISTER(CloneHistNode);
APPTRAVERSE_REGISTER(CloneHistSetValueEvent);
APPTRAVERSE_REGISTER(CloneHistContainer);

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

DiskFileSnapshot SnapshotEntireStorageTree(
    std::filesystem::path const& storage_root) {
  DiskFileSnapshot snapshot;
  if (!std::filesystem::exists(storage_root)) {
    return snapshot;
  }
  for (auto const& entry :
       std::filesystem::recursive_directory_iterator(storage_root)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    auto const rel =
        std::filesystem::relative(entry.path(), storage_root).generic_string();
    std::ifstream in{entry.path(), std::ios::binary | std::ios::ate};
    if (!in) {
      Fail("failed to read storage tree file");
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

DiskStores OpenDiskStores(std::filesystem::path const& user_root,
                          std::filesystem::path const& backing_root,
                          std::set<ae::ObjId> const& overlay_constants) {
  auto user =
      std::make_unique<apptraverse::DirectoryDomainStorage>(user_root);
  auto backing =
      std::make_unique<apptraverse::DirectoryDomainStorage>(backing_root);
  auto overlay = std::make_unique<apptraverse::OverlayDomainStorage>(
      *user, *backing, overlay_constants);
  return DiskStores{std::move(user), std::move(backing), std::move(overlay)};
}

struct BackingPqcSnapshot {
  DiskFileSnapshot p;
  DiskFileSnapshot q;
  DiskFileSnapshot c;
};

BackingPqcSnapshot SnapshotBackingPqc(std::filesystem::path const& backing_root) {
  return BackingPqcSnapshot{
      SnapshotDiskObjectTree(backing_root, kOriginP),
      SnapshotDiskObjectTree(backing_root, kOriginQ),
      SnapshotDiskObjectTree(backing_root, kOriginC),
  };
}

bool operator==(BackingPqcSnapshot const& left, BackingPqcSnapshot const& right) {
  return left.p == right.p && left.q == right.q && left.c == right.c;
}

BackingPqcSnapshot SnapshotBackingHist(std::filesystem::path const& backing_root) {
  return BackingPqcSnapshot{
      SnapshotDiskObjectTree(backing_root, kHistOriginP),
      SnapshotDiskObjectTree(backing_root, kHistOriginQ),
      SnapshotDiskObjectTree(backing_root, kHistOriginC),
  };
}

void CollectHistoricalIds(apptraverse::Node& root,
                          std::set<ae::ObjId::Type>& base_ids,
                          std::set<ae::ObjId::Type>& event_ids) {
  std::vector<apptraverse::Node*> nodes;
  apptraverse::CollectReachableNodes(root, nodes);
  for (apptraverse::Node* node : nodes) {
    if (node->base.is_valid()) {
      base_ids.insert(node->base.id().id());
    }
    for (auto const& record : node->journal) {
      if (record.event.is_valid()) {
        event_ids.insert(record.event.id().id());
      }
    }
  }
}

bool TargetUserContainsAnyId(std::filesystem::path const& user_root,
                             std::set<ae::ObjId::Type> const& ids) {
  for (ae::ObjId::Type id : ids) {
    if (DiskObjectHasAnyLayers(user_root, ae::ObjId{id})) {
      return true;
    }
  }
  return false;
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

  DiskStores stores =
      OpenDiskStores(state_root / "user", state_root / "backing", {kPrefabId});
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
    DiskStores cold = OpenDiskStores(state_root / "user", state_root / "backing",
                                      {kPrefabId});
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

  std::filesystem::path const backing_root = state_root / "backing";
  std::filesystem::path const source_user = state_root / "source" / "user";
  std::filesystem::path const target_user = state_root / "target" / "user";
  std::set<ae::ObjId> const overlay_constants{kOriginP, kOriginQ, kOriginC};
  BackingPqcSnapshot const backing_before = SnapshotBackingPqc(backing_root);

  ae::ObjId clone_r_id{};
  ae::ObjId clone_d_id{};

  {
    DiskStores source_stores =
        OpenDiskStores(source_user, backing_root, overlay_constants);
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
        SnapshotDiskObjectTree(source_user, kRootR);
    DiskFileSnapshot const source_d_before =
        SnapshotDiskObjectTree(source_user, kChildD);

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
        OpenDiskStores(target_user, backing_root, overlay_constants);
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
    clone_r_id = kCloneR;
    clone_d_id = clone_r->child.id();
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

    if (DiskObjectHasAnyLayers(target_user, kOriginP) ||
        DiskObjectHasAnyLayers(target_user, kOriginQ) ||
        DiskObjectHasAnyLayers(target_user, kOriginC)) {
      Fail("target USER must not contain P/Q/C");
    }

    DiskFileSnapshot const source_r_after = SnapshotDiskObjectTree(source_user, kRootR);
    DiskFileSnapshot const source_d_after = SnapshotDiskObjectTree(source_user, kChildD);
    if (source_r_after != source_r_before || source_d_after != source_d_before) {
      Fail("source USER R/D changed after clone");
    }
  }

  {
    DiskStores cold =
        OpenDiskStores(target_user, backing_root, overlay_constants);
    ae::Domain cold_domain{*cold.overlay};
    ae::DomainGraph cold_graph{&cold_domain};
    ae::Ptr<ae::Obj> cold_root;
    if (!cold_graph.LoadRoot(clone_r_id, cold_root).IsOk()) {
      Fail("cold LoadRoot clone R");
    }
    ae::Ptr<CloneGraphTargetV1> cold_r{cold_root};
    ae::Ptr<CloneFacetTargetV1> cold_d{cold_r->child.Load()};
    if (cold_r->value != kGraphMemoryRootValue ||
        cold_d->value != kGraphMemoryChildValue ||
        cold_r->origin_id() != kOriginP || cold_d->origin_id() != kOriginQ ||
        cold_r->child.id() != clone_d_id ||
        cold_r->child_id_snapshot != kChildD) {
      Fail("cold clone graph fields");
    }
    if (!cold_r->shared_const.is_valid() ||
        cold_r->shared_const.id() != kOriginC) {
      Fail("cold shared_const id");
    }
    if (cold_r->shared_const.is_loaded()) {
      Fail("cold shared_const must stay unloaded before Load");
    }
    if (cold_domain.Find(kOriginP) || cold_domain.Find(kOriginQ) ||
        cold_domain.Find(kOriginC)) {
      Fail("P/Q/C materialized before explicit Load(C)");
    }

    ae::Ptr<CloneFacetTargetV1> loaded_c{
        ae::Ptr<ae::Obj>{cold_r->shared_const.Load()}};
    if (!loaded_c) {
      Fail("shared_const.Load() failed");
    }
    if (loaded_c->obj_id != kOriginC || loaded_c->domain != &cold_domain) {
      Fail("loaded C id or domain");
    }
    if (loaded_c->value != kGraphSavedConstValue ||
        loaded_c->tag != kGraphMemoryConstTag) {
      Fail("loaded C fields from BACKING");
    }
    if (cold_domain.Find(kOriginP) || cold_domain.Find(kOriginQ)) {
      Fail("P/Q materialized after Load(C)");
    }
    if (DiskObjectHasAnyLayers(target_user, kOriginC)) {
      Fail("Load(C) must not write C to target USER");
    }
  }

  BackingPqcSnapshot const backing_after = SnapshotBackingPqc(backing_root);
  if (backing_after != backing_before) {
    Fail("BACKING P/Q/C changed");
  }

  std::filesystem::remove_all(state_root);
}

void SeedHistOriginsOnBacking(std::filesystem::path const& backing_root) {
  std::filesystem::create_directories(backing_root);
  apptraverse::DirectoryDomainStorage backing{backing_root};
  ae::Domain domain{backing};
  CloneHistNode::ptr p = CloneHistNode::ptr::Create(
      ae::CreateWith{domain}.with_id(kHistOriginP));
  ae::Ptr<CloneHistNode> p_body = p.Load();
  p_body->value = kHistSavedRootValue;
  apptraverse::InitializeRuntimeNode(*p_body);
  CloneHistNode::ptr q = CloneHistNode::ptr::Create(
      ae::CreateWith{domain}.with_id(kHistOriginQ));
  ae::Ptr<CloneHistNode> q_body = q.Load();
  q_body->value = kHistSavedChildValue;
  apptraverse::InitializeRuntimeNode(*q_body);
  CloneHistConst::ptr c = CloneHistConst::ptr::Create(
      ae::CreateWith{domain}.with_id(kHistOriginC));
  ae::Ptr<CloneHistConst> c_body = c.Load();
  c_body->value = kHistConstValue;
  CloneHistContainer::ptr container_origin = CloneHistContainer::ptr::Create(
      ae::CreateWith{domain}.with_id(kHistOriginContainer));
  ae::DomainGraph graph{&domain};
  backing.BeginSaveSession();
  if (!graph.SaveRoot(p_body, kHistOriginP).IsOk() ||
      !graph.SaveRoot(q_body, kHistOriginQ).IsOk() ||
      !graph.SaveRoot(c_body, kHistOriginC).IsOk() ||
      !graph.SaveRoot(container_origin.Load(), kHistOriginContainer).IsOk()) {
    Fail("SaveRoot hist P/Q/C/M on BACKING");
  }
  if (backing.SaveSessionHadFailure()) {
    Fail("hist backing save session failed");
  }
}

void TestCanonicalCloneNodeGraphWithFreshHistory() {
  std::filesystem::path const state_root =
      std::filesystem::temp_directory_path() /
      "apptraverse_object_clone_node_test";
  std::filesystem::remove_all(state_root);
  std::filesystem::create_directories(state_root / "source" / "user");
  std::filesystem::create_directories(state_root / "target" / "user");
  std::filesystem::path const backing_root = state_root / "backing";
  std::filesystem::path const source_user = state_root / "source" / "user";
  std::filesystem::path const target_user = state_root / "target" / "user";
  std::set<ae::ObjId> const overlay_constants{kHistOriginP, kHistOriginQ,
                                              kHistOriginC};
  SeedHistOriginsOnBacking(backing_root);
  BackingPqcSnapshot const backing_before =
      SnapshotBackingHist(backing_root);

  ae::ObjId clone_r_id{};
  ae::ObjId clone_d_id{};
  std::set<ae::ObjId::Type> source_base_ids;
  std::set<ae::ObjId::Type> source_event_ids;

  {
    DiskStores source_stores =
        OpenDiskStores(source_user, backing_root, overlay_constants);
    ae::Domain source_domain{*source_stores.overlay};
    CloneHistNode::ptr child = CloneHistNode::ptr::Create(
        ae::CreateWith{source_domain}.with_id(kHistChildD));
    ae::Ptr<CloneHistNode> child_body = child.Load();
    child_body->value = kHistInitialChildValue;
    child_body->SetOriginId(kHistOriginQ);
    child_body->shared_const = apptraverse::LocalPtr<CloneHistConst>(
        CloneHistConst::ptr::Declare(
            ae::CreateWith{&source_domain}
                .with_id(kHistOriginC)
                .with_flags(ae::ObjFlags::kUnloadedByDefault |
                            ae::ObjFlags::kUnloaded)));
    apptraverse::InitializeRuntimeNode(*child_body);

    CloneHistNode::ptr root = CloneHistNode::ptr::Create(
        ae::CreateWith{source_domain}.with_id(kHistRootR));
    ae::Ptr<CloneHistNode> root_body = root.Load();
    root_body->value = kHistInitialRootValue;
    root_body->child = child;
    root_body->SetOriginId(kHistOriginP);
    apptraverse::InitializeRuntimeNode(*root_body);

    auto saved_root_event =
        CloneHistSetValueEvent::ptr::Create(ae::CreateWith{source_domain});
    saved_root_event->value = kHistSavedRootValue;
    root_body->Commit(saved_root_event);
    auto saved_child_event =
        CloneHistSetValueEvent::ptr::Create(ae::CreateWith{source_domain});
    saved_child_event->value = kHistSavedChildValue;
    child_body->Commit(saved_child_event);

    source_stores.user->BeginSaveSession();
    ae::DomainGraph source_save{&source_domain};
    if (!source_save.SaveRoot(root_body, kHistRootR).IsOk() ||
        !source_save.SaveRoot(child_body, kHistChildD).IsOk()) {
      Fail("SaveRoot hist R/D on source USER");
    }
    if (source_stores.user->SaveSessionHadFailure()) {
      Fail("hist source USER save failed");
    }

    CollectHistoricalIds(*root_body, source_base_ids, source_event_ids);
    if (source_base_ids.empty()) {
      Fail("source must have historical bases before clone");
    }

    DiskFileSnapshot const source_r_before =
        SnapshotDiskObjectTree(source_user, kHistRootR);
    DiskFileSnapshot const source_d_before =
        SnapshotDiskObjectTree(source_user, kHistChildD);

    ae::DomainGraph source_load{&source_domain};
    ae::Ptr<ae::Obj> loaded_r;
    if (!source_load.LoadRoot(kHistRootR, loaded_r).IsOk()) {
      Fail("LoadRoot hist R before clone");
    }
    ae::Ptr<CloneHistNode> mutable_r{loaded_r};
    auto memory_root_event =
        CloneHistSetValueEvent::ptr::Create(ae::CreateWith{source_domain});
    memory_root_event->value = kHistMemoryRootValue;
    mutable_r->Commit(memory_root_event);
    auto memory_child_event =
        CloneHistSetValueEvent::ptr::Create(ae::CreateWith{source_domain});
    memory_child_event->value = kHistMemoryChildValue;
    mutable_r->child.Load()->Commit(memory_child_event);

    DiskStores target_stores =
        OpenDiskStores(target_user, backing_root, overlay_constants);
    ae::Domain target_domain{*target_stores.overlay};

    apptraverse::CloneOptions options{};
    options.stable_origin = kHistOriginP;
    options.boundary_ids = {kHistOriginP, kHistOriginQ, kHistOriginC};
    options.new_root_id = kHistCloneR;

    target_stores.user->BeginSaveSession();
    apptraverse::CloneCanonicalResult const cloned =
        apptraverse::CloneCanonicalRoot(source_domain, *mutable_r, target_domain,
                                        options);
    if (!cloned || cloned.new_root_id() != kHistCloneR) {
      Fail("CloneCanonicalRoot hist graph");
    }
    if (target_stores.user->SaveSessionHadFailure()) {
      Fail("hist target USER save failed");
    }

    ae::Ptr<CloneHistNode> clone_r{ae::Ptr<ae::Obj>{cloned.root.Load()}};
    clone_r_id = kHistCloneR;
    clone_d_id = clone_r->child.id();
    ae::Ptr<CloneHistNode> clone_d{clone_r->child.Load()};
    if (clone_r->value != kHistMemoryRootValue ||
        clone_d->value != kHistMemoryChildValue ||
        !clone_r->journal.empty() || !clone_d->journal.empty() ||
        !clone_r->base.is_valid() || !clone_d->base.is_valid()) {
      Fail("clone hist node state after Clone");
    }
    ae::Ptr<CloneHistNode> clone_r_base{clone_r->base.Load()};
    ae::Ptr<CloneHistNode> clone_d_base{clone_d->base.Load()};
    ae::Ptr<CloneHistNode> source_r_base{mutable_r->base.Load()};
    ae::Ptr<CloneHistNode> source_d_base{mutable_r->child.Load()->base.Load()};
    if (clone_r->base.id() == source_r_base->obj_id ||
        clone_d->base.id() == source_d_base->obj_id) {
      Fail("clone bases must be new objects");
    }
    if (clone_r_base->value != kHistMemoryRootValue ||
        clone_d_base->value != kHistMemoryChildValue) {
      Fail("clone bases must capture current state");
    }
    if (clone_r_base->origin_id() != kHistOriginP ||
        clone_d_base->origin_id() != kHistOriginQ) {
      Fail("clone base origins");
    }
    if (!clone_d->shared_const.is_valid() ||
        clone_d->shared_const.id() != kHistOriginC) {
      Fail("clone shared_const id");
    }

    if (TargetUserContainsAnyId(target_user, source_base_ids) ||
        TargetUserContainsAnyId(target_user, source_event_ids) ||
        DiskObjectHasAnyLayers(target_user, kHistOriginP) ||
        DiskObjectHasAnyLayers(target_user, kHistOriginQ) ||
        DiskObjectHasAnyLayers(target_user, kHistOriginC)) {
      Fail("target USER must not contain source history or P/Q/C");
    }

    DiskFileSnapshot const source_r_after =
        SnapshotDiskObjectTree(source_user, kHistRootR);
    DiskFileSnapshot const source_d_after =
        SnapshotDiskObjectTree(source_user, kHistChildD);
    if (source_r_after != source_r_before || source_d_after != source_d_before) {
      Fail("hist source USER changed after clone");
    }

    auto post_commit_root =
        CloneHistSetValueEvent::ptr::Create(ae::CreateWith{target_domain});
    post_commit_root->value = kHistPostCloneCommitValue;
    clone_r->Commit(post_commit_root);
    clone_r->ReplayFromBase();
    if (clone_r->value != kHistPostCloneCommitValue) {
      Fail("ReplayFromBase changed clone R");
    }

    ae::DomainGraph target_save{&target_domain};
    target_stores.user->BeginSaveSession();
    if (!target_save.SaveRoot(clone_r, clone_r_id).IsOk()) {
      Fail("SaveRoot clone R after Commit");
    }
    if (target_stores.user->SaveSessionHadFailure()) {
      Fail("target USER save after post-clone Commit");
    }
  }

  {
    DiskStores cold =
        OpenDiskStores(target_user, backing_root, overlay_constants);
    ae::Domain cold_domain{*cold.overlay};
    ae::DomainGraph cold_graph{&cold_domain};
    ae::Ptr<ae::Obj> cold_root;
    if (!cold_graph.LoadRoot(clone_r_id, cold_root).IsOk()) {
      Fail("cold LoadRoot hist clone R");
    }
    ae::Ptr<CloneHistNode> cold_r{cold_root};
    ae::Ptr<CloneHistNode> cold_d{cold_r->child.Load()};
    if (cold_r->value != kHistPostCloneCommitValue ||
        cold_d->value != kHistMemoryChildValue ||
        cold_r->child.id() != clone_d_id ||
        cold_r->origin_id() != kHistOriginP ||
        cold_d->origin_id() != kHistOriginQ ||
        cold_d->journal.empty() == false) {
      Fail("cold hist clone fields");
    }
    if (!cold_r->base.is_valid() || !cold_d->base.is_valid()) {
      Fail("cold hist clone missing fresh bases");
    }
    cold_r->ReplayFromBase();
    if (cold_r->value != kHistPostCloneCommitValue) {
      Fail("cold ReplayFromBase changed R");
    }
    if (!cold_d->shared_const.is_valid() ||
        cold_d->shared_const.id() != kHistOriginC ||
        cold_d->shared_const.is_loaded()) {
      Fail("cold shared_const before Load");
    }
    ae::Ptr<CloneHistConst> loaded_c{
        ae::Ptr<ae::Obj>{cold_d->shared_const.Load()}};
    if (!loaded_c || loaded_c->obj_id != kHistOriginC ||
        loaded_c->value != kHistConstValue) {
      Fail("cold Load(C)");
    }

    auto cold_commit =
        CloneHistSetValueEvent::ptr::Create(ae::CreateWith{cold_domain});
    cold_commit->value = kHistPostCloneCommitValue + 1u;
    cold_r->Commit(cold_commit);
    if (cold_r->value != kHistPostCloneCommitValue + 1u) {
      Fail("cold Commit after load");
    }
  }

  BackingPqcSnapshot const backing_after = SnapshotBackingHist(backing_root);
  if (backing_after != backing_before) {
    Fail("hist BACKING P/Q/C changed");
  }

  std::filesystem::remove_all(state_root);
}

void TestCanonicalCloneObjContainerWithNestedNodeHistory() {
  std::filesystem::path const state_root =
      std::filesystem::temp_directory_path() /
      "apptraverse_object_clone_obj_container_test";
  std::filesystem::remove_all(state_root);
  std::filesystem::create_directories(state_root / "source" / "user");
  std::filesystem::create_directories(state_root / "target" / "user");
  std::filesystem::path const backing_root = state_root / "backing";
  std::filesystem::path const source_user = state_root / "source" / "user";
  std::filesystem::path const target_user = state_root / "target" / "user";
  std::set<ae::ObjId> const overlay_constants{
      kHistOriginP, kHistOriginQ, kHistOriginC, kHistOriginContainer};
  SeedHistOriginsOnBacking(backing_root);
  BackingPqcSnapshot const backing_before =
      SnapshotBackingHist(backing_root);
  DiskFileSnapshot const backing_m_before =
      SnapshotDiskObjectTree(backing_root, kHistOriginContainer);

  ae::ObjId clone_container_id{};
  ae::ObjId clone_child_id{};
  std::set<ae::ObjId::Type> source_base_ids;
  std::set<ae::ObjId::Type> source_event_ids;

  {
    DiskStores source_stores =
        OpenDiskStores(source_user, backing_root, overlay_constants);
    ae::Domain source_domain{*source_stores.overlay};

    CloneHistNode::ptr child = CloneHistNode::ptr::Create(
        ae::CreateWith{source_domain}.with_id(kHistContainerChild));
    ae::Ptr<CloneHistNode> child_body = child.Load();
    child_body->value = kHistInitialChildValue;
    child_body->SetOriginId(kHistOriginQ);
    apptraverse::InitializeRuntimeNode(*child_body);

    auto saved_child_event =
        CloneHistSetValueEvent::ptr::Create(ae::CreateWith{source_domain});
    saved_child_event->value = kHistSavedChildValue;
    child_body->Commit(saved_child_event);

    CloneHistContainer::ptr container = CloneHistContainer::ptr::Create(
        ae::CreateWith{source_domain}.with_id(kHistContainerRoot));
    ae::Ptr<CloneHistContainer> container_body = container.Load();
    container_body->child = child;
    container_body->SetOriginId(kHistOriginContainer);

    source_stores.user->BeginSaveSession();
    ae::DomainGraph source_save{&source_domain};
    if (!source_save.SaveRoot(container_body, kHistContainerRoot).IsOk() ||
        !source_save.SaveRoot(child_body, kHistContainerChild).IsOk()) {
      Fail("SaveRoot hist container/child on source USER");
    }
    if (source_stores.user->SaveSessionHadFailure()) {
      Fail("hist container source USER save failed");
    }

    CollectHistoricalIds(*child_body, source_base_ids, source_event_ids);
    if (source_base_ids.empty()) {
      Fail("nested child must have source base before clone");
    }

    DiskFileSnapshot const source_container_before =
        SnapshotDiskObjectTree(source_user, kHistContainerRoot);
    DiskFileSnapshot const source_child_before =
        SnapshotDiskObjectTree(source_user, kHistContainerChild);

    ae::DomainGraph source_load{&source_domain};
    ae::Ptr<ae::Obj> loaded_container;
    if (!source_load.LoadRoot(kHistContainerRoot, loaded_container).IsOk()) {
      Fail("LoadRoot hist container before clone");
    }
    ae::Ptr<CloneHistContainer> mutable_container{loaded_container};
    ae::Ptr<CloneHistNode> mutable_child{mutable_container->child.Load()};
    auto memory_child_event =
        CloneHistSetValueEvent::ptr::Create(ae::CreateWith{source_domain});
    memory_child_event->value = kHistContainerChildMemoryValue;
    mutable_child->Commit(memory_child_event);

    DiskStores target_stores =
        OpenDiskStores(target_user, backing_root, overlay_constants);
    ae::Domain target_domain{*target_stores.overlay};

    apptraverse::CloneOptions options{};
    options.stable_origin = kHistOriginContainer;
    options.boundary_ids = {kHistOriginP, kHistOriginQ, kHistOriginC,
                            kHistOriginContainer};
    options.new_root_id = kHistCloneContainer;

    target_stores.user->BeginSaveSession();
    apptraverse::CloneCanonicalResult const cloned =
        apptraverse::CloneCanonicalRoot(source_domain, *mutable_container,
                                        target_domain, options);
    if (!cloned || cloned.new_root_id() != kHistCloneContainer) {
      Fail("CloneCanonicalRoot obj container");
    }
    if (target_stores.user->SaveSessionHadFailure()) {
      Fail("obj container target USER save failed");
    }

    ae::Ptr<CloneHistContainer> clone_container{
        ae::Ptr<ae::Obj>{cloned.root.Load()}};
    clone_container_id = kHistCloneContainer;
    if (ae::Registry::GetRegistry().GenerationDistance(
            apptraverse::Node::kClassId,
            clone_container->GetClassId()) >= 0) {
      Fail("clone root must remain ae::Obj, not Node");
    }
    ae::Ptr<CloneHistNode> clone_child{clone_container->child.Load()};
    clone_child_id = clone_child->obj_id;
    if (clone_child->value != kHistContainerChildMemoryValue ||
        clone_child->origin_id() != kHistOriginQ || !clone_child->journal.empty() ||
        !clone_child->base.is_valid()) {
      Fail("clone nested child state after Clone");
    }
    ae::Ptr<CloneHistNode> clone_child_base{clone_child->base.Load()};
    ae::Ptr<CloneHistNode> source_child_base{mutable_child->base.Load()};
    if (clone_child->base.id() == source_child_base->obj_id ||
        clone_child_base->value != kHistContainerChildMemoryValue ||
        clone_child_base->origin_id() != kHistOriginQ) {
      Fail("clone nested child base");
    }

    if (TargetUserContainsAnyId(target_user, source_base_ids) ||
        TargetUserContainsAnyId(target_user, source_event_ids)) {
      Fail("target USER must not contain source child history ids");
    }

    DiskFileSnapshot const source_container_after =
        SnapshotDiskObjectTree(source_user, kHistContainerRoot);
    DiskFileSnapshot const source_child_after =
        SnapshotDiskObjectTree(source_user, kHistContainerChild);
    if (source_container_after != source_container_before ||
        source_child_after != source_child_before) {
      Fail("source USER changed after obj container clone");
    }

    auto post_commit =
        CloneHistSetValueEvent::ptr::Create(ae::CreateWith{target_domain});
    post_commit->value = kHistContainerPostCloneCommitValue;
    clone_child->Commit(post_commit);
    clone_child->ReplayFromBase();
    if (clone_child->value != kHistContainerPostCloneCommitValue) {
      Fail("ReplayFromBase changed nested child");
    }

    ae::DomainGraph target_save{&target_domain};
    target_stores.user->BeginSaveSession();
    if (!target_save.SaveRoot(clone_container, clone_container_id).IsOk()) {
      Fail("SaveRoot clone container after Commit");
    }
    if (target_stores.user->SaveSessionHadFailure()) {
      Fail("target USER save after nested Commit");
    }
  }

  {
    DiskStores cold =
        OpenDiskStores(target_user, backing_root, overlay_constants);
    ae::Domain cold_domain{*cold.overlay};
    ae::DomainGraph cold_graph{&cold_domain};
    ae::Ptr<ae::Obj> cold_root;
    if (!cold_graph.LoadRoot(clone_container_id, cold_root).IsOk()) {
      Fail("cold LoadRoot clone container");
    }
    ae::Ptr<CloneHistContainer> cold_container{cold_root};
    ae::Ptr<CloneHistNode> cold_child{cold_container->child.Load()};
    if (cold_child->obj_id != clone_child_id ||
        cold_child->value != kHistContainerPostCloneCommitValue ||
        cold_child->origin_id() != kHistOriginQ ||
        !cold_child->base.is_valid()) {
      Fail("cold nested child fields");
    }
    cold_child->ReplayFromBase();
    if (cold_child->value != kHistContainerPostCloneCommitValue) {
      Fail("cold ReplayFromBase changed nested child");
    }

    auto cold_commit =
        CloneHistSetValueEvent::ptr::Create(ae::CreateWith{cold_domain});
    cold_commit->value = kHistContainerPostCloneCommitValue + 1u;
    cold_child->Commit(cold_commit);
    if (cold_child->value != kHistContainerPostCloneCommitValue + 1u) {
      Fail("cold Commit on nested child");
    }
  }

  BackingPqcSnapshot const backing_after = SnapshotBackingHist(backing_root);
  if (backing_after != backing_before ||
      SnapshotDiskObjectTree(backing_root, kHistOriginContainer) !=
          backing_m_before) {
    Fail("BACKING changed after obj container clone");
  }

  std::filesystem::remove_all(state_root);
}

void TestCanonicalCloneUnloadedNodeChild() {
  std::filesystem::path const state_root =
      std::filesystem::temp_directory_path() /
      "apptraverse_object_clone_unloaded_child_test";
  std::filesystem::remove_all(state_root);
  std::filesystem::create_directories(state_root / "source" / "user");
  std::filesystem::create_directories(state_root / "target" / "user");
  std::filesystem::path const backing_root = state_root / "backing";
  std::filesystem::path const source_user = state_root / "source" / "user";
  std::filesystem::path const target_user = state_root / "target" / "user";
  std::set<ae::ObjId> const overlay_constants{
      kHistOriginP, kHistOriginQ, kHistOriginC, kHistOriginContainer};
  SeedHistOriginsOnBacking(backing_root);
  BackingPqcSnapshot const backing_before =
      SnapshotBackingHist(backing_root);

  std::set<ae::ObjId::Type> source_base_ids;
  std::set<ae::ObjId::Type> source_event_ids;
  ae::ObjId clone_child_id{};
  ae::ObjId clone_root_id{kHistCloneLazyRootRaw};

  {
    DiskStores prep_stores =
        OpenDiskStores(source_user, backing_root, overlay_constants);
    ae::Domain prep_domain{*prep_stores.overlay};

    CloneHistNode::ptr child = CloneHistNode::ptr::Create(
        ae::CreateWith{prep_domain}.with_id(kHistLazyChild));
    ae::Ptr<CloneHistNode> child_body = child.Load();
    child_body->value = kHistInitialChildValue;
    child_body->SetOriginId(kHistOriginQ);
    apptraverse::InitializeRuntimeNode(*child_body);
    auto saved_event =
        CloneHistSetValueEvent::ptr::Create(ae::CreateWith{prep_domain});
    saved_event->value = kHistLazyChildSavedValue;
    child_body->Commit(saved_event);
    auto memory_event =
        CloneHistSetValueEvent::ptr::Create(ae::CreateWith{prep_domain});
    memory_event->value = kHistLazyChildMemoryValue;
    child_body->Commit(memory_event);

    CloneHistContainer::ptr container = CloneHistContainer::ptr::Create(
        ae::CreateWith{prep_domain}.with_id(kHistLazyRoot));
    ae::Ptr<CloneHistContainer> container_body = container.Load();
    container_body->SetOriginId(kHistOriginContainer);
    container_body->child = CloneHistNode::ptr::Declare(
        ae::CreateWith{&prep_domain}
            .with_id(kHistLazyChild)
            .with_flags(kLazyChildFlags));

    prep_stores.user->BeginSaveSession();
    ae::DomainGraph prep_save{&prep_domain};
    if (!prep_save.SaveRoot(child_body, kHistLazyChild).IsOk() ||
        !prep_save.SaveRoot(container_body, kHistLazyRoot).IsOk()) {
      Fail("SaveRoot lazy prep child/container");
    }
    if (prep_stores.user->SaveSessionHadFailure()) {
      Fail("lazy prep USER save failed");
    }

    CollectHistoricalIds(*child_body, source_base_ids, source_event_ids);
  }

  DiskFileSnapshot const source_r_before =
      SnapshotDiskObjectTree(source_user, kHistLazyRoot);
  DiskFileSnapshot const source_d_before =
      SnapshotDiskObjectTree(source_user, kHistLazyChild);

  {
    DiskStores source_stores =
        OpenDiskStores(source_user, backing_root, overlay_constants);
    ae::Domain source_domain{*source_stores.overlay};
    ae::DomainGraph source_load{&source_domain};
    ae::Ptr<ae::Obj> loaded_root;
    if (!source_load.LoadRoot(kHistLazyRoot, loaded_root).IsOk()) {
      Fail("LoadRoot lazy source container only");
    }
    ae::Ptr<CloneHistContainer> source_container{loaded_root};
    if (!source_container->child.is_valid() ||
        source_container->child.is_loaded()) {
      Fail("source child must stay unloaded");
    }
    if (source_domain.Find(kHistLazyChild)) {
      Fail("source must not materialize lazy child");
    }

    DiskStores target_stores =
        OpenDiskStores(target_user, backing_root, overlay_constants);
    ae::Domain target_domain{*target_stores.overlay};

    apptraverse::CloneOptions options{};
    options.stable_origin = kHistOriginContainer;
    options.boundary_ids = {kHistOriginP, kHistOriginQ, kHistOriginC,
                            kHistOriginContainer};
    options.new_root_id = kHistCloneLazyRoot;

    target_stores.user->BeginSaveSession();
    apptraverse::CloneCanonicalResult const cloned =
        apptraverse::CloneCanonicalRoot(source_domain, *source_container,
                                        target_domain, options);
    if (!cloned || cloned.new_root_id() != kHistCloneLazyRoot) {
      Fail("CloneCanonicalRoot lazy child");
    }
    if (target_stores.user->SaveSessionHadFailure()) {
      Fail("lazy target USER save failed");
    }

    ae::Ptr<CloneHistContainer> clone_container{
        ae::Ptr<ae::Obj>{cloned.root.Load()}};
    clone_child_id = clone_container->child.id();
    if (clone_child_id == kHistLazyChild || !clone_container->child.is_valid() ||
        clone_container->child.is_loaded() ||
        (clone_container->child.flags() & kLazyChildFlags) != kLazyChildFlags) {
      Fail("clone lazy child link");
    }
    if (source_container->child.is_loaded() ||
        source_domain.Find(kHistLazyChild)) {
      Fail("source mutated or materialized after clone");
    }
    if (!DiskObjectHasAnyLayers(target_user, clone_child_id)) {
      Fail("target USER missing unloaded child layers before Load");
    }

    if (TargetUserContainsAnyId(target_user, source_base_ids) ||
        TargetUserContainsAnyId(target_user, source_event_ids)) {
      Fail("target USER must not contain source history ids");
    }

    DiskFileSnapshot const source_r_after =
        SnapshotDiskObjectTree(source_user, kHistLazyRoot);
    DiskFileSnapshot const source_d_after =
        SnapshotDiskObjectTree(source_user, kHistLazyChild);
    if (source_r_after != source_r_before || source_d_after != source_d_before) {
      Fail("source USER changed after lazy clone");
    }

    auto post_commit =
        CloneHistSetValueEvent::ptr::Create(ae::CreateWith{target_domain});
    post_commit->value = kHistLazyPostCloneCommitValue;
    ae::Ptr<CloneHistNode> clone_child_body{
        ae::Ptr<ae::Obj>{clone_container->child.Load()}};
    clone_child_body->Commit(post_commit);
    clone_child_body->ReplayFromBase();
    if (clone_child_body->value != kHistLazyPostCloneCommitValue) {
      Fail("lazy clone ReplayFromBase");
    }

    ae::DomainGraph target_save{&target_domain};
    target_stores.user->BeginSaveSession();
    if (!target_save.SaveRoot(clone_container, clone_root_id).IsOk()) {
      Fail("SaveRoot lazy clone container");
    }
    if (target_stores.user->SaveSessionHadFailure()) {
      Fail("lazy target save after Commit");
    }
  }

  {
    DiskStores cold =
        OpenDiskStores(target_user, backing_root, overlay_constants);
    ae::Domain cold_domain{*cold.overlay};
    ae::DomainGraph cold_graph{&cold_domain};
    ae::Ptr<ae::Obj> cold_root;
    if (!cold_graph.LoadRoot(clone_root_id, cold_root).IsOk()) {
      Fail("cold LoadRoot lazy clone container");
    }
    ae::Ptr<CloneHistContainer> cold_container{cold_root};
    if (cold_container->child.is_loaded()) {
      Fail("cold container child must stay unloaded");
    }
    ae::Ptr<CloneHistNode> cold_child{
        ae::Ptr<ae::Obj>{cold_container->child.Load()}};
    if (cold_child->obj_id != clone_child_id ||
        cold_child->value != kHistLazyPostCloneCommitValue ||
        cold_child->origin_id() != kHistOriginQ ||
        !cold_child->base.is_valid()) {
      Fail("cold lazy child after Load");
    }
    ae::Ptr<CloneHistNode> cold_base{cold_child->base.Load()};
    if (cold_base->origin_id() != kHistOriginQ) {
      Fail("cold lazy child base origin");
    }
    cold_child->ReplayFromBase();
    if (cold_child->value != kHistLazyPostCloneCommitValue) {
      Fail("cold lazy ReplayFromBase");
    }
    auto cold_commit =
        CloneHistSetValueEvent::ptr::Create(ae::CreateWith{cold_domain});
    cold_commit->value = kHistLazyPostCloneCommitValue + 1u;
    cold_child->Commit(cold_commit);
    if (cold_child->value != kHistLazyPostCloneCommitValue + 1u) {
      Fail("cold lazy Commit after load");
    }
  }

  BackingPqcSnapshot const backing_after = SnapshotBackingHist(backing_root);
  if (backing_after != backing_before) {
    Fail("BACKING changed after lazy clone");
  }

  std::filesystem::remove_all(state_root);
}

std::set<ae::ObjId> UiWindowPrefabOverlayConstants() {
  return {kUiPrefabTitle, kUiPrefabLocale, kUiPrefabTitleEn, kUiPrefabTitleRu};
}

apptraverse::CloneOptions UiWindowPrefabCloneOptions(ae::ObjId new_root_id) {
  apptraverse::CloneOptions options{};
  options.stable_origin = kUiPrefabWindow;
  options.stable_prefab_ids = {kUiPrefabWindow, kUiPrefabPresenter};
  options.boundary_ids = UiWindowPrefabOverlayConstants();
  options.new_root_id = new_root_id;
  return options;
}

void SeedPresentedWindowPrefabOnBacking(std::filesystem::path const& backing_root) {
  std::filesystem::create_directories(backing_root);
  apptraverse::DirectoryDomainStorage backing{backing_root};
  ae::Domain domain{backing};

  apptraverse::ui::UiDesktopWindow::ptr window =
      apptraverse::ui::UiDesktopWindow::ptr::Create(
          ae::CreateWith{domain}.with_id(kUiPrefabWindow));
  ae::Ptr<apptraverse::ui::UiDesktopWindow> window_body = window.Load();
  auto initial_frame =
      apptraverse::ui::SetUiDesktopWindowFrameEvent::ptr::Create(
          ae::CreateWith{domain});
  initial_frame->x = kUiPrefabFrameX;
  initial_frame->y = kUiPrefabFrameY;
  initial_frame->client_width = kUiPrefabClientWidth;
  initial_frame->client_height = kUiPrefabClientHeight;
  window_body->Apply(*initial_frame);
  apptraverse::InitializeRuntimeNode(*window_body);

  apptraverse::ui::UiLocaleSettings::ptr locale =
      apptraverse::ui::UiLocaleSettings::ptr::Create(
          ae::CreateWith{domain}.with_id(kUiPrefabLocale));
  apptraverse::InitializeRuntimeNode(*locale.Load(), *window_body);

  apptraverse::ui::UiLocalizationString::ptr title =
      apptraverse::ui::UiLocalizationString::ptr::Create(
          ae::CreateWith{domain}.with_id(kUiPrefabTitle));
  apptraverse::InitializeRuntimeNode(*title.Load(), *window_body);
  apptraverse::ui::UiString::ptr title_en =
      apptraverse::ui::UiString::ptr::Create(
          ae::CreateWith{domain}.with_id(kUiPrefabTitleEn));
  title_en->utf8 = "Clone Prefab EN";
  apptraverse::ui::UiString::ptr title_ru =
      apptraverse::ui::UiString::ptr::Create(
          ae::CreateWith{domain}.with_id(kUiPrefabTitleRu));
  title_ru->utf8 = "Clone Prefab RU";
  title.Load()->strings = {title_en, title_ru};

  apptraverse::ui::UiDesktopWindowPresenter::ptr presenter =
      apptraverse::ui::UiDesktopWindowPresenter::ptr::Create(
          ae::CreateWith{domain}.with_id(kUiPrefabPresenter));
  apptraverse::ui::ComposePresentedWindow(window, presenter, title, locale);

  if (window_body->origin_id().is_valid() ||
      presenter.Load()->origin_id().is_valid()) {
    Fail("prefab W/V must not have origin before clone");
  }
  if (window_body->x() != kUiPrefabFrameX ||
      window_body->y() != kUiPrefabFrameY ||
      window_body->client_width() != kUiPrefabClientWidth ||
      window_body->client_height() != kUiPrefabClientHeight) {
    Fail("prefab window geometry");
  }

  ae::DomainGraph graph{&domain};
  backing.BeginSaveSession();
  ae::Ptr<apptraverse::ui::UiDesktopWindowPresenter> presenter_body =
      presenter.Load();
  if (!graph.SaveRoot(window_body, kUiPrefabWindow).IsOk() ||
      !graph.SaveRoot(presenter_body, kUiPrefabPresenter).IsOk() ||
      !graph.SaveRoot(title.Load(), kUiPrefabTitle).IsOk() ||
      !graph.SaveRoot(locale.Load(), kUiPrefabLocale).IsOk() ||
      !graph.SaveRoot(title_en.Load(), kUiPrefabTitleEn).IsOk() ||
      !graph.SaveRoot(title_ru.Load(), kUiPrefabTitleRu).IsOk()) {
    Fail("SaveRoot presented window prefab on BACKING");
  }
  if (backing.SaveSessionHadFailure()) {
    Fail("BACKING save failed for presented window prefab");
  }
}

ae::Ptr<apptraverse::ui::UiDesktopWindowPresenter> LoadWindowPresenter(
    apptraverse::ui::UiDesktopWindow& window) {
  if (!window.presenter.is_valid()) {
    Fail("window missing presenter link");
  }
  ae::Ptr<ae::Obj> presenter_obj{window.presenter.Load()};
  if (!presenter_obj) {
    Fail("presenter Load failed");
  }
  return ae::Ptr<apptraverse::ui::UiDesktopWindowPresenter>{presenter_obj};
}

void ExpectPresentedWindowGeometry(apptraverse::ui::UiDesktopWindow const& window,
                                   std::int32_t x, std::int32_t y,
                                   std::int32_t client_width,
                                   std::int32_t client_height) {
  if (window.x() != x || window.y() != y ||
      window.client_width() != client_width ||
      window.client_height() != client_height) {
    Fail("presented window geometry mismatch");
  }
}

void TestCanonicalClonePresentedWindowPrefab() {
  namespace ui = apptraverse::ui;
  std::filesystem::path const state_root =
      std::filesystem::temp_directory_path() /
      "apptraverse_object_clone_presented_window_test";
  std::filesystem::remove_all(state_root);
  std::filesystem::create_directories(state_root / "instance_a" / "user");
  std::filesystem::create_directories(state_root / "instance_b" / "user");
  std::filesystem::path const backing_root = state_root / "backing";
  std::filesystem::path const instance_a_user = state_root / "instance_a" / "user";
  std::filesystem::path const instance_b_user = state_root / "instance_b" / "user";
  std::set<ae::ObjId> const overlay_constants = UiWindowPrefabOverlayConstants();

  SeedPresentedWindowPrefabOnBacking(backing_root);
  DiskFileSnapshot const backing_before =
      SnapshotEntireStorageTree(backing_root);

  ae::ObjId clone_presenter_a_id{};
  DiskFileSnapshot instance_a_user_before_second_clone;

  {
    DiskStores source_stores = OpenDiskStores(
        instance_a_user, backing_root, overlay_constants);
    ae::Domain source_domain{*source_stores.overlay};
    ae::DomainGraph source_load{&source_domain};
    ae::Ptr<ae::Obj> prefab_root;
    if (!source_load.LoadRoot(kUiPrefabWindow, prefab_root).IsOk()) {
      Fail("LoadRoot prefab window W");
    }
    ae::Ptr<ui::UiDesktopWindow> source_w{prefab_root};

    DiskStores target_stores = OpenDiskStores(
        instance_a_user, backing_root, overlay_constants);
    ae::Domain target_domain{*target_stores.overlay};
    target_stores.user->BeginSaveSession();
    apptraverse::CloneCanonicalResult const cloned =
        apptraverse::CloneCanonicalRoot(source_domain, *source_w, target_domain,
                                        UiWindowPrefabCloneOptions(
                                            kUiCloneInstanceA));
    if (!cloned || cloned.new_root_id() != kUiCloneInstanceA) {
      Fail("CloneCanonicalRoot prefab W to A");
    }
    if (target_stores.user->SaveSessionHadFailure()) {
      Fail("instance A USER save failed");
    }

    ae::Ptr<ui::UiDesktopWindow> instance_a{
        ae::Ptr<ae::Obj>{cloned.root.Load()}};
    ae::Ptr<ui::UiDesktopWindowPresenter> presenter_a =
        LoadWindowPresenter(*instance_a);
    clone_presenter_a_id = presenter_a->obj_id;
    if (instance_a->obj_id == kUiPrefabWindow ||
        clone_presenter_a_id == kUiPrefabPresenter) {
      Fail("first clone must allocate new W/V ids");
    }
    if (presenter_a->window.id() != instance_a->obj_id) {
      Fail("A presenter window back-ref");
    }
    if (instance_a->origin_id() != kUiPrefabWindow ||
        presenter_a->origin_id() != kUiPrefabPresenter) {
      Fail("first clone origins W/V");
    }
    if (presenter_a->title.id() != kUiPrefabTitle ||
        presenter_a->locale.id() != kUiPrefabLocale ||
        presenter_a->title->Resolve(0) != "Clone Prefab EN" ||
        presenter_a->locale->language_index() != 0) {
      Fail("first clone shared title/locale");
    }
    if (presenter_a->presentation_loaded) {
      Fail("first clone presenter must stay unloaded");
    }
    if (!instance_a->base.is_valid() || !instance_a->journal.empty()) {
      Fail("first clone window fresh history shape");
    }
    ae::Ptr<ui::UiDesktopWindow> instance_a_base{instance_a->base.Load()};
    if (instance_a_base->origin_id() != kUiPrefabWindow) {
      Fail("first clone window base origin");
    }
    ExpectPresentedWindowGeometry(*instance_a, kUiPrefabFrameX, kUiPrefabFrameY,
                                  kUiPrefabClientWidth, kUiPrefabClientHeight);
  }

  {
    DiskStores source_stores = OpenDiskStores(
        instance_a_user, backing_root, overlay_constants);
    ae::Domain source_domain{*source_stores.overlay};
    ae::DomainGraph source_load{&source_domain};
    ae::Ptr<ae::Obj> loaded_a;
    if (!source_load.LoadRoot(kUiCloneInstanceA, loaded_a).IsOk()) {
      Fail("LoadRoot instance A for second clone");
    }
    ae::Ptr<ui::UiDesktopWindow> instance_a{loaded_a};
    instance_a_user_before_second_clone =
        SnapshotEntireStorageTree(instance_a_user);

    instance_a->RequestFrame(kUiInstanceMutatedX, kUiInstanceMutatedY,
                             kUiPrefabClientWidth, kUiPrefabClientHeight);
    ExpectPresentedWindowGeometry(*instance_a, kUiInstanceMutatedX,
                                  kUiInstanceMutatedY, kUiPrefabClientWidth,
                                  kUiPrefabClientHeight);

    DiskStores target_stores = OpenDiskStores(
        instance_b_user, backing_root, overlay_constants);
    ae::Domain target_domain{*target_stores.overlay};
    target_stores.user->BeginSaveSession();
    apptraverse::CloneCanonicalResult const cloned =
        apptraverse::CloneCanonicalRoot(source_domain, *instance_a,
                                        target_domain,
                                        UiWindowPrefabCloneOptions(
                                            kUiCloneInstanceB));
    if (!cloned || cloned.new_root_id() != kUiCloneInstanceB) {
      Fail("CloneCanonicalRoot instance A to B");
    }
    if (target_stores.user->SaveSessionHadFailure()) {
      Fail("instance B USER save failed");
    }

    ae::Ptr<ui::UiDesktopWindow> instance_b{
        ae::Ptr<ae::Obj>{cloned.root.Load()}};
    ae::Ptr<ui::UiDesktopWindowPresenter> presenter_b =
        LoadWindowPresenter(*instance_b);
    if (instance_b->obj_id == instance_a->obj_id ||
        presenter_b->obj_id == clone_presenter_a_id) {
      Fail("second clone must allocate new ids");
    }
    if (presenter_b->window.id() != instance_b->obj_id) {
      Fail("B presenter window back-ref");
    }
    if (instance_b->origin_id() != kUiPrefabWindow ||
        presenter_b->origin_id() != kUiPrefabPresenter) {
      Fail("second clone origins must stay W/V not A/VA");
    }
    ExpectPresentedWindowGeometry(*instance_b, kUiInstanceMutatedX,
                                  kUiInstanceMutatedY, kUiPrefabClientWidth,
                                  kUiPrefabClientHeight);
    if (!instance_b->base.is_valid() || !instance_b->journal.empty()) {
      Fail("second clone fresh history on B");
    }

    if (instance_a->origin_id() != kUiPrefabWindow ||
        LoadWindowPresenter(*instance_a)->origin_id() != kUiPrefabPresenter) {
      Fail("source A origins changed by second clone");
    }
    ExpectPresentedWindowGeometry(*instance_a, kUiInstanceMutatedX,
                                  kUiInstanceMutatedY, kUiPrefabClientWidth,
                                  kUiPrefabClientHeight);

    DiskFileSnapshot const instance_a_user_after_second_clone =
        SnapshotEntireStorageTree(instance_a_user);
    if (instance_a_user_after_second_clone !=
        instance_a_user_before_second_clone) {
      Fail("instance A USER changed by second clone");
    }
  }

  if (DiskObjectHasAnyLayers(instance_a_user, kUiPrefabWindow) ||
      DiskObjectHasAnyLayers(instance_a_user, kUiPrefabPresenter) ||
      DiskObjectHasAnyLayers(instance_b_user, kUiPrefabWindow) ||
      DiskObjectHasAnyLayers(instance_b_user, kUiPrefabPresenter)) {
    Fail("USER must not contain prefab W/V bodies");
  }
  for (ae::ObjId const shared_id : overlay_constants) {
    if (DiskObjectHasAnyLayers(instance_a_user, shared_id) ||
        DiskObjectHasAnyLayers(instance_b_user, shared_id)) {
      Fail("USER must not contain shared title/locale bodies");
    }
  }

  {
    DiskStores cold =
        OpenDiskStores(instance_b_user, backing_root, overlay_constants);
    ae::Domain cold_domain{*cold.overlay};
    ae::DomainGraph cold_graph{&cold_domain};
    ae::Ptr<ae::Obj> cold_root;
    if (!cold_graph.LoadRoot(kUiCloneInstanceB, cold_root).IsOk()) {
      Fail("cold LoadRoot instance B");
    }
    ae::Ptr<ui::UiDesktopWindow> cold_b{cold_root};
    ae::Ptr<ui::UiDesktopWindowPresenter> cold_presenter_b =
        LoadWindowPresenter(*cold_b);
    ExpectPresentedWindowGeometry(*cold_b, kUiInstanceMutatedX,
                                  kUiInstanceMutatedY, kUiPrefabClientWidth,
                                  kUiPrefabClientHeight);
    if (cold_presenter_b->window.id() != cold_b->obj_id ||
        cold_b->origin_id() != kUiPrefabWindow ||
        cold_presenter_b->origin_id() != kUiPrefabPresenter) {
      Fail("cold B cycle or origins");
    }
    if (cold_presenter_b->presentation_loaded ||
        cold_presenter_b->title.id() != kUiPrefabTitle ||
        cold_presenter_b->locale.id() != kUiPrefabLocale ||
        cold_presenter_b->title->Resolve(0) != "Clone Prefab EN") {
      Fail("cold B presenter/title/locale");
    }
    if (!cold_b->base.is_valid()) {
      Fail("cold B missing base");
    }
    ae::Ptr<ui::UiDesktopWindow> cold_base{cold_b->base.Load()};
    if (cold_base->origin_id() != kUiPrefabWindow) {
      Fail("cold B base origin");
    }
    cold_b->ReplayFromBase();
    ExpectPresentedWindowGeometry(*cold_b, kUiInstanceMutatedX,
                                  kUiInstanceMutatedY, kUiPrefabClientWidth,
                                  kUiPrefabClientHeight);
  }

  DiskFileSnapshot const backing_after = SnapshotEntireStorageTree(backing_root);
  if (backing_after != backing_before) {
    Fail("BACKING changed after presented window clone scenario");
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
  (void)&g_apptraverse_registrar_CloneHistConst;
  (void)&g_apptraverse_registrar_CloneHistNode;
  (void)&g_apptraverse_registrar_CloneHistSetValueEvent;
  (void)&g_apptraverse_registrar_CloneHistContainer;

  TestCanonicalCloneSingleObjectMemoryRoundtrip();
  TestCanonicalCloneTwoObjectGraphWithConstBoundary();
  TestCanonicalCloneNodeGraphWithFreshHistory();
  TestCanonicalCloneObjContainerWithNestedNodeHistory();
  TestCanonicalCloneUnloadedNodeChild();
  TestCanonicalClonePresentedWindowPrefab();
  TestCloneMissingStableOrigin();
  TestCloneObjIdConflict();

  std::cout << "object_clone_test OK\n";
  return 0;
}
