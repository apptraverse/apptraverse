#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj.h"
#include "aether-objects/obj/version_iterator.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/event.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/overlay_domain_storage.h"
#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_presenters.h"

namespace {

using StoredLayerSet =
    std::map<std::uint32_t, std::set<std::uint8_t>>;

StoredLayerSet CollectStoredLayers(ae::RamDomainStorage const& storage,
                                   ae::ObjId object_id) {
  StoredLayerSet layers;
  auto const obj_it = storage.state.find(object_id);
  if (obj_it == storage.state.end() || !obj_it->second.has_value()) {
    return layers;
  }
  for (auto const& [class_id, versions] : *obj_it->second) {
    for (auto const& [version, data] : versions) {
      (void)data;
      layers[class_id].insert(version);
    }
  }
  return layers;
}

bool HasStoredLayer(ae::RamDomainStorage const& storage, ae::ObjId object_id,
                    std::uint32_t class_id, std::uint8_t version) {
  auto const obj_it = storage.state.find(object_id);
  if (obj_it == storage.state.end() || !obj_it->second.has_value()) {
    return false;
  }
  auto const class_it = obj_it->second->find(class_id);
  if (class_it == obj_it->second->end()) {
    return false;
  }
  return class_it->second.find(version) != class_it->second.end();
}

class OriginFacetTargetV0 : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::OriginFacetTargetV0",
                           OriginFacetTargetV0, ae::Obj, 0)

 protected:
  OriginFacetTargetV0() = default;

 public:
  explicit OriginFacetTargetV0(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(value))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, value);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, value);
  }

  std::uint32_t value{0};
};

class OriginFacetTargetV1 : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::OriginFacetTargetV1",
                           OriginFacetTargetV1, ae::Obj, 1)

 protected:
  OriginFacetTargetV1() = default;

 public:
  explicit OriginFacetTargetV1(ae::ObjProp prop) : Obj{prop} {}

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

class OriginFacetTargetV1Gated : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::OriginFacetTargetV1Gated",
                           OriginFacetTargetV1Gated, ae::Obj, 2)

 protected:
  OriginFacetTargetV1Gated() = default;

 public:
  explicit OriginFacetTargetV1Gated(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(value), AE_MMBR(tag), AE_MMBR(write_derived_s1))

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

  bool ShouldSaveVersion(ae::Version<1>) const { return write_derived_s1; }

  std::uint32_t value{0};
  std::uint32_t tag{0};
  bool write_derived_s1{true};
};

// No own versioned Load/Save: persisted only through reflection; Obj must not
// contribute Load/Save overloads that would select the versioned path.
class OriginFacetReflectOnly : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::OriginFacetReflectOnly",
                           OriginFacetReflectOnly, ae::Obj, 0)

 protected:
  OriginFacetReflectOnly() = default;

 public:
  explicit OriginFacetReflectOnly(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(value), AE_MMBR(tag))

  std::uint32_t value{0};
  std::uint32_t tag{0};
};

// S1 is written only when the instance owns it; otherwise the layer comes
// from origin on load.
class OriginLayerItem : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::OriginLayerItem",
                           OriginLayerItem, ae::Obj, 1)

 protected:
  OriginLayerItem() = default;

 public:
  explicit OriginLayerItem(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(value), AE_MMBR(tag), AE_MMBR(own_tag))

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
    dnv(base_, tag, own_tag);
  }

  template <typename Dnv>
  void Save(ae::Version<1>, Dnv& dnv) const {
    dnv(base_, tag, own_tag);
  }

  bool ShouldSaveVersion(ae::Version<1>) const { return own_tag; }

  std::uint32_t value{0};
  std::uint32_t tag{0};
  bool own_tag{false};
};

APPTRAVERSE_REGISTER(OriginFacetTargetV0);
APPTRAVERSE_REGISTER(OriginFacetTargetV1);
APPTRAVERSE_REGISTER(OriginFacetTargetV1Gated);
APPTRAVERSE_REGISTER(OriginFacetReflectOnly);
APPTRAVERSE_REGISTER(OriginLayerItem);

static_assert(!ae::VersionSaveTrait<OriginFacetTargetV0, 1>::value,
              "OriginFacetTargetV0 must not expose public Save(V1)");
static_assert(ae::HasAnyVersionedSave_v<OriginFacetTargetV1>);
static_assert(ae::VersionSaveTrait<OriginFacetTargetV1, 1>::value);
static_assert(!ae::HasAnyVersionedSave_v<ae::Obj>);
static_assert(!ae::HasAnyVersionedLoad_v<ae::Obj>);
static_assert(!ae::HasAnyVersionedSave_v<OriginFacetReflectOnly>);
static_assert(!ae::HasAnyVersionedLoad_v<OriginFacetReflectOnly>);

void Fail(char const* message) {
  std::cerr << "object_origin_facet_test FAIL: " << message << '\n';
  std::abort();
}

bool PrefabBodyAbsent(ae::RamDomainStorage const& storage, ae::ObjId prefab_id) {
  auto const it = storage.state.find(prefab_id);
  if (it == storage.state.end() || !it->second.has_value()) {
    return true;
  }
  return it->second->empty();
}

std::set<std::uint8_t> ObjLayers(StoredLayerSet const& layers) {
  auto const it = layers.find(ae::Obj::kClassId);
  return it == layers.end() ? std::set<std::uint8_t>{} : it->second;
}

void ExpectObjS0Only(ae::RamDomainStorage const& storage, ae::ObjId object_id,
                     char const* message) {
  if (ObjLayers(CollectStoredLayers(storage, object_id)) !=
      std::set<std::uint8_t>{0}) {
    Fail(message);
  }
}

void TestObjS0CarriesOriginRoundtrip() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};

  ae::ObjId const prefab_id{100u};
  ae::ObjId const instance_id{200u};
  ae::ObjId const plain_id{201u};

  OriginFacetTargetV0::ptr instance = OriginFacetTargetV0::ptr::Create(
      ae::CreateWith{domain}.with_id(instance_id));
  ae::Obj& instance_obj = *instance.Load();
  instance_obj.SetOriginId(prefab_id);
  OriginFacetTargetV0::ptr plain = OriginFacetTargetV0::ptr::Create(
      ae::CreateWith{domain}.with_id(plain_id));
  ae::Obj& plain_obj = *plain.Load();

  ae::DomainGraph graph{&domain};
  if (!graph.SaveVersion(ae::Version<0>{},
                         static_cast<ae::Obj const&>(instance_obj), instance_id)
           .IsOk() ||
      !graph.SaveVersion(ae::Version<0>{},
                         static_cast<ae::Obj const&>(plain_obj), plain_id)
           .IsOk()) {
    Fail("SaveVersion Obj/S0");
  }
  ExpectObjS0Only(storage, instance_id, "instance Obj layers are not {0}");
  ExpectObjS0Only(storage, plain_id, "plain Obj layers are not {0}");
  auto const& instance_obj_s0 =
      storage.state.at(instance_id)->at(ae::Obj::kClassId).at(0);
  auto const& plain_obj_s0 =
      storage.state.at(plain_id)->at(ae::Obj::kClassId).at(0);
  if (instance_obj_s0 == plain_obj_s0) {
    Fail("Obj/S0 payload does not carry origin");
  }
  if (!PrefabBodyAbsent(storage, prefab_id)) {
    Fail("prefab body written via origin save");
  }

  domain.RemoveObject(&instance_obj);
  OriginFacetTargetV0::ptr reloaded = OriginFacetTargetV0::ptr::Create(
      ae::CreateWith{domain}.with_id(instance_id));
  ae::Obj& loaded = *reloaded.Load();
  if (!graph.LoadVersion(ae::Version<0>{}, loaded, instance_id).IsOk()) {
    Fail("LoadVersion Obj/S0");
  }
  if (loaded.origin_id() != prefab_id) {
    Fail("origin_id mismatch after LoadVersion");
  }
  // Origin remains lazy (no LoadRoot on P during Obj/S0 load).
  if (domain.Find(prefab_id)) {
    Fail("prefab P was materialized during Obj/S0 load");
  }
}

void TestOrdinarySaveWithoutOriginFacet() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  ae::ObjId const instance_id{300u};

  OriginFacetTargetV0::ptr instance = OriginFacetTargetV0::ptr::Create(
      ae::CreateWith{domain}.with_id(instance_id));
  instance.Load()->value = 7;

  ae::DomainGraph graph{&domain};
  if (!graph.Save(*instance.Load(), instance_id).has_value()) {
    Fail("Save<T> returned nullopt");
  }
  if (!HasStoredLayer(storage, instance_id, OriginFacetTargetV0::kClassId, 0)) {
    Fail("missing S0 after ordinary Save");
  }
  ExpectObjS0Only(storage, instance_id, "ordinary Save: Obj layers are not {0}");
  if (HasStoredLayer(storage, instance_id, OriginFacetTargetV0::kClassId, 1)) {
    Fail("Derived/S1 without public Save(V1)");
  }
}

void TestDerivedWithOwnV1DoesNotWriteObjFacetWithoutOrigin() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  ae::ObjId const instance_id{400u};

  OriginFacetTargetV1::ptr instance = OriginFacetTargetV1::ptr::Create(
      ae::CreateWith{domain}.with_id(instance_id));
  ae::Ptr<OriginFacetTargetV1> body = instance.Load();
  body->value = 1;
  body->tag = 2;

  ae::DomainGraph graph{&domain};
  if (!graph.Save(*body, instance_id).has_value()) {
    Fail("Save derived V1 returned nullopt");
  }
  if (!HasStoredLayer(storage, instance_id, OriginFacetTargetV1::kClassId, 0)) {
    Fail("missing derived S0");
  }
  if (!HasStoredLayer(storage, instance_id, OriginFacetTargetV1::kClassId, 1)) {
    Fail("missing derived S1");
  }
  ExpectObjS0Only(storage, instance_id,
                  "Save<DerivedWithV1> without origin: Obj layers are not {0}");
}

void TestSaveWithOriginWritesOnlyObjS0() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  ae::ObjId const prefab_id{408u};
  ae::ObjId const instance_id{409u};

  OriginFacetTargetV0::ptr instance = OriginFacetTargetV0::ptr::Create(
      ae::CreateWith{domain}.with_id(instance_id));
  instance.Load()->value = 42;
  instance.Load()->SetOriginId(prefab_id);

  ae::DomainGraph graph{&domain};
  if (!graph.Save(*instance.Load(), instance_id).has_value()) {
    Fail("SaveRoot path Save<T> failed");
  }
  if (!HasStoredLayer(storage, instance_id, OriginFacetTargetV0::kClassId, 0)) {
    Fail("missing Derived/S0 after auto facet Save");
  }
  ExpectObjS0Only(storage, instance_id,
                  "Save with origin: Obj layers are not {0}");
  if (HasStoredLayer(storage, instance_id, OriginFacetTargetV0::kClassId, 1)) {
    Fail("spurious Derived/S1");
  }
  if (!PrefabBodyAbsent(storage, prefab_id)) {
    Fail("prefab body written to USER");
  }
}

void TestDerivedS0S1WithOriginInObjS0() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  ae::ObjId const prefab_id{410u};
  ae::ObjId const instance_id{411u};

  OriginFacetTargetV1::ptr instance = OriginFacetTargetV1::ptr::Create(
      ae::CreateWith{domain}.with_id(instance_id));
  ae::Ptr<OriginFacetTargetV1> body = instance.Load();
  body->value = 1;
  body->tag = 2;
  body->SetOriginId(prefab_id);

  ae::DomainGraph graph{&domain};
  if (!graph.Save(*body, instance_id).has_value()) {
    Fail("Save derived with origin failed");
  }
  if (!HasStoredLayer(storage, instance_id, OriginFacetTargetV1::kClassId, 0) ||
      !HasStoredLayer(storage, instance_id, OriginFacetTargetV1::kClassId, 1)) {
    Fail("missing Derived S0/S1 layers");
  }
  ExpectObjS0Only(storage, instance_id,
                  "Derived S0/S1 with origin: Obj layers are not {0}");
}

void TestRepeatSaveRootDoesNotAddExtraLayers() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  ae::ObjId const prefab_id{412u};
  ae::ObjId const instance_id{413u};

  OriginFacetTargetV0::ptr instance = OriginFacetTargetV0::ptr::Create(
      ae::CreateWith{domain}.with_id(instance_id));
  instance.Load()->value = 1;
  instance.Load()->SetOriginId(prefab_id);
  ae::Ptr<OriginFacetTargetV0> body = instance.Load();
  ae::DomainGraph first_graph{&domain};
  if (!first_graph.Save(*body, instance_id).has_value()) {
    Fail("initial Save failed");
  }
  StoredLayerSet const after_first =
      CollectStoredLayers(storage, instance_id);
  for (int i = 0; i < 2; ++i) {
    ae::DomainGraph graph{&domain};
    if (!graph.Save(*body, instance_id).has_value()) {
      Fail("repeat Save failed");
    }
    if (CollectStoredLayers(storage, instance_id) != after_first) {
      Fail("repeat Save changed stored layer set");
    }
  }
}

template <typename Fn>
void WithColdLoadV0(ae::RamDomainStorage& storage, ae::ObjId instance_id,
                    Fn&& fn) {
  ae::Domain domain{storage};
  ae::DomainGraph graph{&domain};
  ae::Ptr<ae::Obj> root = graph.LoadRoot(instance_id);
  if (!root || root->GetClassId() != OriginFacetTargetV0::kClassId) {
    Fail("LoadRoot failed for OriginFacetTargetV0");
  }
  ae::Ptr<OriginFacetTargetV0> loaded{root};
  std::forward<Fn>(fn)(domain, graph, loaded);
}

template <typename Fn>
void WithColdLoadV1(ae::RamDomainStorage& storage, ae::ObjId instance_id,
                    Fn&& fn) {
  ae::Domain domain{storage};
  ae::DomainGraph graph{&domain};
  ae::Ptr<ae::Obj> root = graph.LoadRoot(instance_id);
  if (!root || root->GetClassId() != OriginFacetTargetV1::kClassId) {
    Fail("LoadRoot failed for OriginFacetTargetV1");
  }
  ae::Ptr<OriginFacetTargetV1> loaded{root};
  std::forward<Fn>(fn)(domain, graph, loaded);
}

void TestColdLoadRootAppliesObjS0OnDerived() {
  ae::RamDomainStorage storage;
  ae::ObjId const prefab_id{600u};
  ae::ObjId const instance_id{601u};

  {
    ae::Domain domain{storage};
    OriginFacetTargetV0::ptr instance = OriginFacetTargetV0::ptr::Create(
        ae::CreateWith{domain}.with_id(instance_id));
    instance.Load()->value = 99;
    ae::Obj& obj = *instance.Load();
    obj.SetOriginId(prefab_id);
    ae::DomainGraph graph{&domain};
    if (!graph.Save(*instance.Load(), instance_id).has_value()) {
      Fail("Save Derived/S0 before cold load");
    }
    ExpectObjS0Only(storage, instance_id,
                    "Obj layers are not {0} before cold load");
    (void)obj;
  }

  WithColdLoadV0(storage, instance_id,
                 [prefab_id](ae::Domain& domain, ae::DomainGraph&,
                             ae::Ptr<OriginFacetTargetV0> loaded) {
                   if (loaded->value != 99 || loaded->origin_id() != prefab_id) {
                     Fail("cold LoadRoot field/origin mismatch");
                   }
                   if (domain.Find(prefab_id)) {
                     Fail("prefab P materialized during cold LoadRoot");
                   }
                 });
}

void TestColdLoadWithoutOrigin() {
  ae::RamDomainStorage storage;
  ae::ObjId const instance_id{602u};
  {
    ae::Domain domain{storage};
    OriginFacetTargetV0::ptr instance = OriginFacetTargetV0::ptr::Create(
        ae::CreateWith{domain}.with_id(instance_id));
    instance.Load()->value = 11;
    ae::DomainGraph graph{&domain};
    if (!graph.Save(*instance.Load(), instance_id).has_value()) {
      Fail("Save without origin");
    }
  }
  WithColdLoadV0(storage, instance_id,
                 [](ae::Domain&, ae::DomainGraph&,
                    ae::Ptr<OriginFacetTargetV0> loaded) {
                   if (loaded->value != 11) {
                     Fail("cold load without origin");
                   }
                   if (loaded->origin_id().is_valid()) {
                     Fail("unexpected origin after load without origin");
                   }
                 });
}

void TestColdLoadDerivedS1WithOrigin() {
  ae::RamDomainStorage storage;
  ae::ObjId const prefab_id{604u};
  ae::ObjId const instance_id{605u};
  {
    ae::Domain domain{storage};
    OriginFacetTargetV1::ptr instance = OriginFacetTargetV1::ptr::Create(
        ae::CreateWith{domain}.with_id(instance_id));
    ae::Ptr<OriginFacetTargetV1> body = instance.Load();
    body->value = 5;
    body->tag = 6;
    ae::Obj& obj = *body;
    obj.SetOriginId(prefab_id);
    ae::DomainGraph graph{&domain};
    if (!graph.Save(*body, instance_id).has_value()) {
      Fail("Save Derived S0/S1");
    }
    ExpectObjS0Only(storage, instance_id,
                    "Obj layers are not {0} alongside Derived/S1");
    (void)obj;
  }
  WithColdLoadV1(storage, instance_id,
                 [prefab_id](ae::Domain&, ae::DomainGraph&,
                             ae::Ptr<OriginFacetTargetV1> loaded) {
                   if (loaded->value != 5 || loaded->tag != 6 ||
                       loaded->origin_id() != prefab_id) {
                     Fail("Derived S0/S1 + Obj/S0 cold load mismatch");
                   }
                 });
}

bool UserHasAnyLayers(ae::RamDomainStorage const& user, ae::ObjId object_id) {
  return !PrefabBodyAbsent(user, object_id);
}

bool BackingStateUnchanged(ae::RamDomainStorage const& backing,
                           ae::RamDomainStorage::ObjClassData const& snapshot) {
  return backing.state == snapshot;
}

constexpr ae::ObjId::Type kDiskPrefabIdRaw = 81001u;
constexpr ae::ObjId::Type kDiskInstanceIdRaw = 81002u;
constexpr std::uint32_t kDiskValue = 150u;
constexpr std::uint32_t kDiskTag = 251u;

ae::ObjId const kDiskPrefabId{kDiskPrefabIdRaw};
ae::ObjId const kDiskInstanceId{kDiskInstanceIdRaw};

std::filesystem::path DiskBackingRoot(std::filesystem::path const& state_root) {
  return state_root / "backing";
}

std::filesystem::path DiskUserRoot(std::filesystem::path const& state_root) {
  return state_root / "user";
}

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

void ExpectDiskPrefabBackingLayers(StoredLayerSet const& layers) {
  auto const derived_it = layers.find(OriginFacetTargetV1::kClassId);
  if (derived_it == layers.end() ||
      derived_it->second != StoredLayerSet::mapped_type{0, 1}) {
    Fail("BACKING/P unexpected Derived S layers");
  }
  for (auto const& [class_id, versions] : layers) {
    if (class_id == OriginFacetTargetV1::kClassId) {
      continue;
    }
    if (class_id == ae::Obj::kClassId) {
      if (versions != std::set<std::uint8_t>{0}) {
        Fail("BACKING/P Obj layers are not {0}");
      }
      continue;
    }
    Fail("BACKING/P unexpected ClassId");
  }
}

void ExpectDiskInstanceUserLayers(StoredLayerSet const& layers) {
  if (layers.size() != 2u) {
    Fail("USER/A unexpected ClassId count");
  }
  if (layers.at(OriginFacetTargetV1::kClassId) !=
      StoredLayerSet::mapped_type{0, 1}) {
    Fail("USER/A unexpected Derived S layers");
  }
  if (layers.at(ae::Obj::kClassId) != std::set<std::uint8_t>{0}) {
    Fail("USER/A unexpected Obj S layers");
  }
}

void RunDiskOriginWrite(std::filesystem::path const& state_root) {
  std::filesystem::create_directories(state_root);
  std::filesystem::remove_all(DiskBackingRoot(state_root));
  std::filesystem::remove_all(DiskUserRoot(state_root));
  std::filesystem::create_directories(DiskBackingRoot(state_root));
  std::filesystem::create_directories(DiskUserRoot(state_root));

  {
    apptraverse::DirectoryDomainStorage backing{DiskBackingRoot(state_root)};
    ae::Domain domain{backing};
    OriginFacetTargetV1::ptr prefab = OriginFacetTargetV1::ptr::Create(
        ae::CreateWith{domain}.with_id(kDiskPrefabId));
    ae::Ptr<OriginFacetTargetV1> prefab_body = prefab.Load();
    prefab_body->value = kDiskValue;
    prefab_body->tag = kDiskTag;
    ae::DomainGraph graph{&domain};
    backing.BeginSaveSession();
    if (!graph.SaveRoot(prefab_body, kDiskPrefabId).IsOk()) {
      Fail("disk write: SaveRoot P");
    }
    if (backing.SaveSessionHadFailure()) {
      Fail("disk write: backing save session failed");
    }
    if (prefab_body->origin_id().is_valid()) {
      Fail("disk write: P must not have origin");
    }
  }

  ExpectDiskPrefabBackingLayers(
      CollectDiskLayers(DiskBackingRoot(state_root), kDiskPrefabId));

  apptraverse::DirectoryDomainStorage user_storage{DiskUserRoot(state_root)};
  apptraverse::DirectoryDomainStorage backing_storage{
      DiskBackingRoot(state_root)};
  apptraverse::OverlayDomainStorage overlay{
      user_storage, backing_storage, std::set<ae::ObjId>{kDiskPrefabId}};

  {
    ae::Domain domain{overlay};
    OriginFacetTargetV1::ptr instance = OriginFacetTargetV1::ptr::Create(
        ae::CreateWith{domain}.with_id(kDiskInstanceId));
    ae::Ptr<OriginFacetTargetV1> instance_body = instance.Load();
    instance_body->value = kDiskValue;
    instance_body->tag = kDiskTag;
    instance_body->SetOriginId(kDiskPrefabId);
    ae::DomainGraph graph{&domain};
    user_storage.BeginSaveSession();
    if (!graph.SaveRoot(instance_body, kDiskInstanceId).IsOk()) {
      Fail("disk write: SaveRoot A");
    }
    if (user_storage.SaveSessionHadFailure()) {
      Fail("disk write: USER save session failed");
    }
  }

  ExpectDiskInstanceUserLayers(
      CollectDiskLayers(DiskUserRoot(state_root), kDiskInstanceId));
  if (DiskObjectHasAnyLayers(DiskUserRoot(state_root), kDiskPrefabId)) {
    Fail("disk write: USER must not contain P layers");
  }
}

void RunDiskOriginRead(std::filesystem::path const& state_root) {
  apptraverse::DirectoryDomainStorage user_storage{DiskUserRoot(state_root)};
  apptraverse::DirectoryDomainStorage backing_storage{
      DiskBackingRoot(state_root)};
  apptraverse::OverlayDomainStorage overlay{
      user_storage, backing_storage, std::set<ae::ObjId>{kDiskPrefabId}};

  DiskFileSnapshot const backing_p_before =
      SnapshotDiskObjectTree(DiskBackingRoot(state_root), kDiskPrefabId);
  StoredLayerSet const user_a_before =
      CollectDiskLayers(DiskUserRoot(state_root), kDiskInstanceId);

  {
    ae::Domain domain{overlay};
    ae::DomainGraph graph{&domain};
    ae::Ptr<ae::Obj> instance_root;
    if (!graph.LoadRoot(kDiskInstanceId, instance_root).IsOk()) {
      Fail("disk read: LoadRoot A");
    }
    ae::Ptr<OriginFacetTargetV1> instance{instance_root};
    if (instance->obj_id != kDiskInstanceId ||
        instance->GetClassId() != OriginFacetTargetV1::kClassId) {
      Fail("disk read: A identity");
    }
    if (instance->value != kDiskValue || instance->tag != kDiskTag ||
        instance->origin_id() != kDiskPrefabId) {
      Fail("disk read: A fields or origin");
    }
    if (domain.Find(kDiskPrefabId)) {
      Fail("disk read: P materialized during LoadRoot(A)");
    }

    user_storage.BeginSaveSession();
    ae::DomainGraph save_graph{&domain};
    if (!save_graph.SaveRoot(instance, kDiskInstanceId).IsOk()) {
      Fail("disk read: repeat SaveRoot A");
    }
    if (user_storage.SaveSessionHadFailure()) {
      Fail("disk read: USER save session failed");
    }
  }

  StoredLayerSet const user_a_after =
      CollectDiskLayers(DiskUserRoot(state_root), kDiskInstanceId);
  if (user_a_after != user_a_before) {
    Fail("disk read: repeat SaveRoot changed USER/A layers");
  }
  if (DiskObjectHasAnyLayers(DiskUserRoot(state_root), kDiskPrefabId)) {
    Fail("disk read: USER contains P layers");
  }
  DiskFileSnapshot const backing_p_after =
      SnapshotDiskObjectTree(DiskBackingRoot(state_root), kDiskPrefabId);
  if (backing_p_after != backing_p_before) {
    Fail("disk read: BACKING/P changed after repeat SaveRoot(A)");
  }
}

void TestFinalOriginRoundtripV0() {
  ae::RamDomainStorage storage;
  ae::ObjId const P_id{700u};
  ae::ObjId const A_id{701u};
  ae::ObjId const no_origin_id{702u};

  StoredLayerSet layers_with_origin;
  {
    ae::Domain domain{storage};
    OriginFacetTargetV0::ptr instance = OriginFacetTargetV0::ptr::Create(
        ae::CreateWith{domain}.with_id(A_id));
    instance.Load()->value = 77;
    instance.Load()->SetOriginId(P_id);
    ae::DomainGraph graph{&domain};
    ae::Ptr<ae::Obj> root = instance.Load();
    if (!graph.SaveRoot(root, A_id).IsOk()) {
      Fail("SaveRoot A with origin");
    }
    if (!HasStoredLayer(storage, A_id, OriginFacetTargetV0::kClassId, 0)) {
      Fail("SaveRoot missing Derived/S0");
    }
    ExpectObjS0Only(storage, A_id, "SaveRoot with origin: Obj layers are not {0}");
    if (HasStoredLayer(storage, A_id, OriginFacetTargetV0::kClassId, 1)) {
      Fail("spurious Derived/S1 on V0 instance");
    }
    if (!PrefabBodyAbsent(storage, P_id)) {
      Fail("prefab P body written to storage");
    }
    layers_with_origin = CollectStoredLayers(storage, A_id);

    OriginFacetTargetV0::ptr plain = OriginFacetTargetV0::ptr::Create(
        ae::CreateWith{domain}.with_id(no_origin_id));
    plain.Load()->value = 5;
    if (!graph.SaveRoot(plain.Load(), no_origin_id).IsOk()) {
      Fail("SaveRoot without origin");
    }
    ExpectObjS0Only(storage, no_origin_id,
                    "SaveRoot without origin: Obj layers are not {0}");
  }

  WithColdLoadV0(
      storage, A_id,
      [&](ae::Domain& domain, ae::DomainGraph& graph,
          ae::Ptr<OriginFacetTargetV0> loaded) {
        if (loaded->obj_id != A_id ||
            loaded->GetClassId() != OriginFacetTargetV0::kClassId) {
          Fail("cold LoadRoot A identity mismatch");
        }
        if (loaded->value != 77 || loaded->origin_id() != P_id) {
          Fail("cold LoadRoot A fields/origin mismatch");
        }
        if (domain.Find(P_id)) {
          Fail("P materialized after cold LoadRoot");
        }
        ae::Ptr<ae::Obj> root = loaded;
        if (!graph.SaveRoot(root, A_id).IsOk()) {
          Fail("repeat SaveRoot after cold load");
        }
        if (CollectStoredLayers(storage, A_id) != layers_with_origin) {
          Fail("repeat SaveRoot changed layer set");
        }
      });

  WithColdLoadV0(storage, no_origin_id,
                 [](ae::Domain&, ae::DomainGraph&,
                    ae::Ptr<OriginFacetTargetV0> loaded) {
                   if (loaded->origin_id().is_valid()) {
                     Fail("no-origin instance has origin after load");
                   }
                 });
}

void TestFinalOriginRoundtripV1WithShouldSaveVersion() {
  ae::RamDomainStorage storage;
  ae::ObjId const P_id{710u};
  ae::ObjId const A_id{711u};
  ae::ObjId const gated_id{712u};

  StoredLayerSet layers_full;
  {
    ae::Domain domain{storage};
    OriginFacetTargetV1::ptr instance = OriginFacetTargetV1::ptr::Create(
        ae::CreateWith{domain}.with_id(A_id));
    ae::Ptr<OriginFacetTargetV1> body = instance.Load();
    body->value = 8;
    body->tag = 9;
    body->SetOriginId(P_id);
    ae::DomainGraph graph{&domain};
    if (!graph.SaveRoot(body, A_id).IsOk()) {
      Fail("SaveRoot Derived V1 with origin");
    }
    if (!HasStoredLayer(storage, A_id, OriginFacetTargetV1::kClassId, 0) ||
        !HasStoredLayer(storage, A_id, OriginFacetTargetV1::kClassId, 1)) {
      Fail("missing Derived S0/S1");
    }
    ExpectObjS0Only(storage, A_id, "V1 with origin: Obj layers are not {0}");
    if (!PrefabBodyAbsent(storage, P_id)) {
      Fail("V1 roundtrip: P body in storage");
    }
    layers_full = CollectStoredLayers(storage, A_id);

    OriginFacetTargetV1Gated::ptr gated = OriginFacetTargetV1Gated::ptr::Create(
        ae::CreateWith{domain}.with_id(gated_id));
    ae::Ptr<OriginFacetTargetV1Gated> gated_body = gated.Load();
    gated_body->value = 1;
    gated_body->tag = 2;
    gated_body->write_derived_s1 = false;
    gated_body->SetOriginId(P_id);
    if (!graph.SaveRoot(gated_body, gated_id).IsOk()) {
      Fail("SaveRoot gated Derived S1 off");
    }
    if (!HasStoredLayer(storage, gated_id, OriginFacetTargetV1Gated::kClassId,
                        0)) {
      Fail("gated missing S0");
    }
    if (HasStoredLayer(storage, gated_id, OriginFacetTargetV1Gated::kClassId,
                       1)) {
      Fail("ShouldSaveVersion(false) still wrote Derived/S1");
    }
    ExpectObjS0Only(storage, gated_id, "gated: Obj layers are not {0}");
  }

  WithColdLoadV1(
      storage, A_id,
      [&](ae::Domain& domain, ae::DomainGraph& graph,
          ae::Ptr<OriginFacetTargetV1> loaded) {
        if (loaded->obj_id != A_id ||
            loaded->GetClassId() != OriginFacetTargetV1::kClassId ||
            loaded->value != 8 || loaded->tag != 9 ||
            loaded->origin_id() != P_id) {
          Fail("V1 cold LoadRoot mismatch");
        }
        if (domain.Find(P_id)) {
          Fail("V1 cold load materialized P");
        }
        if (!graph.SaveRoot(loaded, A_id).IsOk()) {
          Fail("V1 repeat SaveRoot");
        }
        if (CollectStoredLayers(storage, A_id) != layers_full) {
          Fail("V1 repeat SaveRoot changed layers");
        }
      });
}

void TestConstPrefabDynamicInstanceRoundtrip() {
  ae::ObjId const P_id{81001u};
  ae::ObjId const A_id{81002u};
  std::uint32_t const kValue = 150u;
  std::uint32_t const kTag = 251u;

  ae::RamDomainStorage backing;
  {
    ae::Domain domain{backing};
    OriginFacetTargetV1::ptr prefab = OriginFacetTargetV1::ptr::Create(
        ae::CreateWith{domain}.with_id(P_id));
    ae::Ptr<OriginFacetTargetV1> prefab_body = prefab.Load();
    prefab_body->value = kValue;
    prefab_body->tag = kTag;
    ae::DomainGraph graph{&domain};
    if (!graph.SaveRoot(prefab_body, P_id).IsOk()) {
      Fail("SaveRoot P to BACKING");
    }
    if (prefab_body->origin_id().is_valid()) {
      Fail("P must not have origin");
    }
    if (!HasStoredLayer(backing, P_id, OriginFacetTargetV1::kClassId, 0) ||
        !HasStoredLayer(backing, P_id, OriginFacetTargetV1::kClassId, 1)) {
      Fail("P missing Derived S0/S1 in BACKING");
    }
    ExpectObjS0Only(backing, P_id, "P Obj layers in BACKING are not {0}");
  }
  ae::RamDomainStorage::ObjClassData const backing_snapshot = backing.state;

  ae::RamDomainStorage user;
  std::set<ae::ObjId> supply_constants{P_id};
  apptraverse::OverlayDomainStorage overlay{user, backing, std::move(supply_constants)};

  StoredLayerSet layers_after_save;
  {
    ae::Domain domain{overlay};
    OriginFacetTargetV1::ptr instance = OriginFacetTargetV1::ptr::Create(
        ae::CreateWith{domain}.with_id(A_id));
    ae::Ptr<OriginFacetTargetV1> instance_body = instance.Load();
    instance_body->value = kValue;
    instance_body->tag = kTag;
    instance_body->SetOriginId(P_id);
    if (A_id == P_id) {
      Fail("A and P must have distinct ObjIds");
    }
    ae::DomainGraph graph{&domain};
    if (!graph.SaveRoot(instance_body, A_id).IsOk()) {
      Fail("SaveRoot dynamic A to USER");
    }
    layers_after_save = CollectStoredLayers(user, A_id);
  }

  if (!HasStoredLayer(user, A_id, OriginFacetTargetV1::kClassId, 0) ||
      !HasStoredLayer(user, A_id, OriginFacetTargetV1::kClassId, 1)) {
    Fail("USER missing A Derived S0/S1");
  }
  if (UserHasAnyLayers(user, P_id)) {
    Fail("USER contains layers for supply-constant P");
  }
  if (!BackingStateUnchanged(backing, backing_snapshot)) {
    Fail("BACKING changed after SaveRoot(A)");
  }
  if (layers_after_save.size() != 2u) {
    Fail("unexpected ClassId count on A in USER");
  }
  if (layers_after_save[OriginFacetTargetV1::kClassId] !=
      StoredLayerSet::mapped_type{0, 1}) {
    Fail("unexpected Derived S versions on A in USER");
  }
  if (ObjLayers(layers_after_save) != std::set<std::uint8_t>{0}) {
    Fail("unexpected Obj S layers on A in USER (expected S0 carrying origin)");
  }

  {
    ae::Domain domain{overlay};
    ae::DomainGraph graph{&domain};
    ae::Ptr<ae::Obj> instance_root;
    if (!graph.LoadRoot(A_id, instance_root).IsOk()) {
      Fail("cold LoadRoot(A) without loading P");
    }
    ae::Ptr<OriginFacetTargetV1> instance{instance_root};
    if (instance->obj_id != A_id ||
        instance->GetClassId() != OriginFacetTargetV1::kClassId) {
      Fail("cold LoadRoot A identity");
    }
    if (instance->value != kValue || instance->tag != kTag ||
        instance->origin_id() != P_id) {
      Fail("cold LoadRoot A value/tag/origin");
    }
    if (domain.Find(P_id)) {
      Fail("P materialized during cold LoadRoot(A)");
    }
    ae::DomainGraph save_graph{&domain};
    if (!save_graph.SaveRoot(instance, A_id).IsOk()) {
      Fail("repeat SaveRoot(A) after cold load");
    }
    if (CollectStoredLayers(user, A_id) != layers_after_save) {
      Fail("repeat SaveRoot(A) changed USER layer set");
    }
  }

  if (UserHasAnyLayers(user, P_id)) {
    Fail("P present in USER after cold load cycle");
  }
  if (!BackingStateUnchanged(backing, backing_snapshot)) {
    Fail("BACKING changed after cold load cycle");
  }
}

void TestLoadRootRejectsObjOnlyStorage() {
  ae::RamDomainStorage storage;
  ae::ObjId const prefab_id{606u};
  ae::ObjId const instance_id{607u};
  {
    ae::Domain domain{storage};
    OriginFacetTargetV0::ptr instance = OriginFacetTargetV0::ptr::Create(
        ae::CreateWith{domain}.with_id(instance_id));
    ae::Obj& obj = *instance.Load();
    obj.SetOriginId(prefab_id);
    ae::DomainGraph graph{&domain};
    if (!graph.SaveVersion(ae::Version<0>{}, static_cast<ae::Obj const&>(obj),
                           instance_id)
             .IsOk()) {
      Fail("write Obj/S0-only storage");
    }
    domain.RemoveObject(&obj);
  }
  ae::Domain domain{storage};
  ae::DomainGraph graph{&domain};
  if (graph.LoadRoot(instance_id)) {
    Fail("LoadRoot must fail for Obj/S0-only storage");
  }
}

template <typename T, typename Fill, typename Check>
void RoundtripWithOrigin(char const* label, ae::ObjId instance_id,
                         ae::ObjId prefab_id, StoredLayerSet const& expected,
                         Fill&& fill, Check&& check) {
  ae::RamDomainStorage storage;
  {
    ae::Domain domain{storage};
    typename T::ptr instance =
        T::ptr::Create(ae::CreateWith{domain}.with_id(instance_id));
    ae::Ptr<T> body = instance.Load();
    fill(*body);
    body->SetOriginId(prefab_id);
    ae::DomainGraph graph{&domain};
    if (!graph.SaveRoot(body, instance_id).IsOk()) {
      std::cerr << label << ": ";
      Fail("SaveRoot with origin");
    }
  }
  StoredLayerSet const stored = CollectStoredLayers(storage, instance_id);
  if (!PrefabBodyAbsent(storage, prefab_id)) {
    std::cerr << label << ": ";
    Fail("prefab body written to storage");
  }

  ae::Domain domain{storage};
  ae::DomainGraph graph{&domain};
  ae::Ptr<ae::Obj> root;
  if (!graph.LoadRoot(instance_id, root).IsOk() || !root ||
      root->GetClassId() != T::kClassId) {
    std::cerr << label << ": ";
    Fail("LoadRoot in a new Domain");
  }
  ae::Ptr<T> loaded{root};
  if (loaded->origin_id() != prefab_id) {
    std::cerr << label << ": ";
    Fail("origin lost after new Domain load");
  }
  if (!check(*loaded)) {
    std::cerr << label << ": ";
    Fail("payload lost after new Domain load");
  }
  if (domain.Find(prefab_id)) {
    std::cerr << label << ": ";
    Fail("prefab materialized during load");
  }
  if (stored != expected) {
    std::cerr << label << ": stored layers";
    for (auto const& [class_id, versions] : stored) {
      for (std::uint8_t const version : versions) {
        std::cerr << ' ' << class_id << '/' << static_cast<int>(version);
      }
    }
    std::cerr << '\n';
    Fail("unexpected stored layer set");
  }
}

void TestNewDomainRoundtripReflectOnlyWithOrigin() {
  RoundtripWithOrigin<OriginFacetReflectOnly>(
      "reflect-only", ae::ObjId{720u}, ae::ObjId{721u},
      StoredLayerSet{{OriginFacetReflectOnly::kClassId, {0}},
                     {ae::Obj::kClassId, {0}}},
      [](OriginFacetReflectOnly& o) {
        o.value = 31;
        o.tag = 32;
      },
      [](OriginFacetReflectOnly const& o) {
        return o.value == 31 && o.tag == 32;
      });
}

void TestNewDomainRoundtripConcreteEventWithOrigin() {
  using Frame = apptraverse::ui::SetUiDesktopWindowFrameEvent;
  RoundtripWithOrigin<Frame>(
      "concrete EventFor", ae::ObjId{730u}, ae::ObjId{731u},
      StoredLayerSet{{Frame::kClassId, {0}},
                     {apptraverse::Event::kClassId, {0}},
                     {ae::Obj::kClassId, {0}}},
      [](Frame& e) {
        e.x = 120;
        e.y = 80;
        e.client_width = 640;
        e.client_height = 480;
      },
      [](Frame const& e) {
        return e.x == 120 && e.y == 80 && e.client_width == 640 &&
               e.client_height == 480;
      });
}

void TestNewDomainRoundtripOwnVersionedWithOrigin() {
  RoundtripWithOrigin<OriginFacetTargetV1>(
      "own versioned Load/Save", ae::ObjId{740u}, ae::ObjId{741u},
      StoredLayerSet{{OriginFacetTargetV1::kClassId, {0, 1}},
                     {ae::Obj::kClassId, {0}}},
      [](OriginFacetTargetV1& o) {
        o.value = 41;
        o.tag = 42;
      },
      [](OriginFacetTargetV1 const& o) {
        return o.value == 41 && o.tag == 42;
      });
}

ae::Ptr<OriginLayerItem> LoadLayerItem(ae::Domain& domain, ae::ObjId id) {
  ae::DomainGraph graph{&domain};
  ae::Ptr<ae::Obj> root;
  if (!graph.LoadRoot(id, root).IsOk() || !root ||
      root->GetClassId() != OriginLayerItem::kClassId) {
    Fail("LoadRoot OriginLayerItem");
  }
  return ae::Ptr<OriginLayerItem>{root};
}

void TestMissingLayerResolvedFromOrigin() {
  ae::ObjId const P_id{750u};
  ae::ObjId const A_id{751u};
  ae::ObjId const B_id{752u};
  ae::ObjId const C_id{753u};

  ae::RamDomainStorage backing;
  {
    ae::Domain domain{backing};
    OriginLayerItem::ptr prefab = OriginLayerItem::ptr::Create(
        ae::CreateWith{domain}.with_id(P_id));
    ae::Ptr<OriginLayerItem> body = prefab.Load();
    body->value = 150;
    body->tag = 252;
    ae::DomainGraph graph{&domain};
    if (!graph.SaveRoot(body, P_id).IsOk() ||
        !graph.SaveVersion(ae::Version<1>{}, *body, P_id).IsOk()) {
      Fail("supply P S0/S1");
    }
  }
  ae::RamDomainStorage::ObjClassData const backing_snapshot = backing.state;

  ae::RamDomainStorage user;
  apptraverse::OverlayDomainStorage overlay{user, backing,
                                            std::set<ae::ObjId>{P_id}};
  {
    ae::Domain domain{overlay};
    ae::DomainGraph graph{&domain};
    auto make = [&](ae::ObjId id, ae::ObjId origin, std::uint32_t value) {
      OriginLayerItem::ptr item =
          OriginLayerItem::ptr::Create(ae::CreateWith{domain}.with_id(id));
      ae::Ptr<OriginLayerItem> body = item.Load();
      body->value = value;
      body->SetOriginId(origin);
      return body;
    };
    ae::Ptr<OriginLayerItem> a = make(A_id, P_id, 1601);
    ae::Ptr<OriginLayerItem> b = make(B_id, P_id, 150);
    b->tag = 999;
    b->own_tag = true;
    ae::Ptr<OriginLayerItem> c = make(C_id, ae::ObjId{}, 5);
    if (!graph.SaveRoot(a, A_id).IsOk() || !graph.SaveRoot(b, B_id).IsOk() ||
        !graph.SaveRoot(c, C_id).IsOk()) {
      Fail("SaveRoot A/B/C");
    }
  }
  if (HasStoredLayer(user, A_id, OriginLayerItem::kClassId, 1) ||
      !HasStoredLayer(user, B_id, OriginLayerItem::kClassId, 1)) {
    Fail("USER S1 ownership: A must not own S1, B must");
  }
  StoredLayerSet const a_layers = CollectStoredLayers(user, A_id);

  {
    ae::Domain domain{overlay};
    ae::Ptr<OriginLayerItem> a = LoadLayerItem(domain, A_id);
    ae::Ptr<OriginLayerItem> b = LoadLayerItem(domain, B_id);
    ae::Ptr<OriginLayerItem> c = LoadLayerItem(domain, C_id);
    if (a->value != 1601 || a->tag != 252 || a->own_tag) {
      Fail("A: own S0 kept, missing S1 resolved from origin");
    }
    if (b->value != 150 || b->tag != 999 || !b->own_tag) {
      Fail("B: own S1 must take priority over origin S1");
    }
    if (c->value != 5 || c->tag != 0) {
      Fail("C without origin must keep S1 defaults");
    }
    if (domain.Find(P_id)) {
      Fail("P materialized while resolving S1");
    }
    ae::DomainGraph graph{&domain};
    if (!graph.SaveRoot(a, A_id).IsOk()) {
      Fail("resave A");
    }
  }
  if (CollectStoredLayers(user, A_id) != a_layers) {
    Fail("resave copied resolved S1 into USER/A");
  }
  if (UserHasAnyLayers(user, P_id)) {
    Fail("P layers copied into USER");
  }
  if (!BackingStateUnchanged(backing, backing_snapshot)) {
    Fail("BACKING changed");
  }
}

}  // namespace

int main(int argc, char** argv) {
  apptraverse::EnsureObjectRegistration();
  (void)&g_apptraverse_registrar_OriginFacetTargetV0;
  (void)&g_apptraverse_registrar_OriginFacetTargetV1;
  (void)&g_apptraverse_registrar_OriginFacetTargetV1Gated;
  (void)&g_apptraverse_registrar_OriginFacetReflectOnly;
  (void)&g_apptraverse_registrar_OriginLayerItem;
  apptraverse::ui::EnsureUiPresenterRegistration();

  if (argc == 3) {
    std::string_view const mode{argv[1]};
    std::filesystem::path const state_root{argv[2]};
    if (mode == "--write") {
      RunDiskOriginWrite(state_root);
      return 0;
    }
    if (mode == "--read") {
      RunDiskOriginRead(state_root);
      return 0;
    }
    Fail("unknown disk mode (expected --write or --read)");
  }
  if (argc != 1) {
    Fail("usage: object_origin_facet_test | --write <state-root> | --read "
         "<state-root>");
  }

  TestObjS0CarriesOriginRoundtrip();
  TestOrdinarySaveWithoutOriginFacet();
  TestDerivedWithOwnV1DoesNotWriteObjFacetWithoutOrigin();
  TestSaveWithOriginWritesOnlyObjS0();
  TestDerivedS0S1WithOriginInObjS0();
  TestRepeatSaveRootDoesNotAddExtraLayers();
  TestColdLoadRootAppliesObjS0OnDerived();
  TestColdLoadWithoutOrigin();
  TestColdLoadDerivedS1WithOrigin();
  TestLoadRootRejectsObjOnlyStorage();
  TestFinalOriginRoundtripV0();
  TestFinalOriginRoundtripV1WithShouldSaveVersion();
  TestConstPrefabDynamicInstanceRoundtrip();
  TestNewDomainRoundtripReflectOnlyWithOrigin();
  TestNewDomainRoundtripConcreteEventWithOrigin();
  TestNewDomainRoundtripOwnVersionedWithOrigin();
  TestMissingLayerResolvedFromOrigin();
  std::cout << "object_origin_facet_test OK\n";
  return 0;
}
