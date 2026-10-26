#include <cstdlib>
#include <iostream>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj.h"
#include "aether-objects/obj/version_iterator.h"

#include "apptraverse/object_macros.h"

namespace {

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

APPTRAVERSE_REGISTER(OriginFacetTargetV0);
APPTRAVERSE_REGISTER(OriginFacetTargetV1);

static_assert(!ae::VersionSaveTrait<OriginFacetTargetV0, 1>::value,
              "OriginFacetTargetV0 must not expose public Save(V1)");
static_assert(ae::HasAnyVersionedSave_v<OriginFacetTargetV1>);
static_assert(ae::VersionSaveTrait<OriginFacetTargetV1, 1>::value);

void Fail(char const* message) {
  std::cerr << "object_origin_facet_test FAIL: " << message << '\n';
  std::abort();
}

void TestManualObjFacetRoundtrip() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};

  ae::ObjId const prefab_id{100u};
  ae::ObjId const instance_id{200u};

  OriginFacetTargetV0::ptr instance = OriginFacetTargetV0::ptr::Create(
      ae::CreateWith{domain}.with_id(instance_id));
  ae::Obj& instance_obj = *instance.Load();
  instance_obj.SetOriginId(prefab_id);

  ae::DomainGraph graph{&domain};
  if (!graph.SaveVersion(ae::Version<1>{}, static_cast<ae::Obj const&>(instance_obj),
                         instance_id)
           .IsOk()) {
    Fail("SaveVersion Obj/S1");
  }

  if (!HasStoredLayer(storage, instance_id, ae::Obj::kClassId, 1)) {
    Fail("missing Obj/S1 layer on instance");
  }
  if (HasStoredLayer(storage, instance_id, OriginFacetTargetV0::kClassId, 1)) {
    Fail("spurious Derived/S1 on reflection-only class instance");
  }
  if (HasStoredLayer(storage, prefab_id, ae::Obj::kClassId, 1)) {
    Fail("Obj/S1 written on prefab id");
  }
  if (HasStoredLayer(storage, prefab_id, OriginFacetTargetV0::kClassId, 1)) {
    Fail("prefab body materialized via origin save");
  }

  domain.RemoveObject(&instance_obj);
  OriginFacetTargetV0::ptr reloaded = OriginFacetTargetV0::ptr::Create(
      ae::CreateWith{domain}.with_id(instance_id));
  ae::Obj& loaded = *reloaded.Load();
  if (!graph.LoadVersion(ae::Version<1>{}, loaded, instance_id).IsOk()) {
    Fail("LoadVersion Obj/S1");
  }
  if (loaded.origin_id() != prefab_id) {
    Fail("origin_id mismatch after LoadVersion");
  }
  if (!loaded.origin_id().is_valid()) {
    Fail("origin not valid after load");
  }
  // Origin remains lazy (no LoadRoot on P during facet load).
  ae::Ptr<ae::Obj> const cached = domain.Find(prefab_id);
  if (cached) {
    Fail("prefab P was materialized during origin facet load");
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
  if (HasStoredLayer(storage, instance_id, ae::Obj::kClassId, 1)) {
    Fail("Obj/S1 without origin on ordinary Save");
  }
  if (HasStoredLayer(storage, instance_id, OriginFacetTargetV0::kClassId, 1)) {
    Fail("Derived/S1 without public Save(V1)");
  }
}

void TestDerivedWithOwnV1DoesNotWriteObjFacet() {
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
  if (HasStoredLayer(storage, instance_id, ae::Obj::kClassId, 1)) {
    Fail("Obj/S1 written implicitly by Save<DerivedWithV1> without origin");
  }
}

void TestShouldSaveVersionBlocksEmptyOrigin() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  ae::ObjId const instance_id{500u};

  OriginFacetTargetV0::ptr instance = OriginFacetTargetV0::ptr::Create(
      ae::CreateWith{domain}.with_id(instance_id));
  ae::Obj const& obj = *instance.Load();
  if (obj.ShouldSaveVersion(ae::Version<1>{})) {
    Fail("ShouldSaveVersion true without origin");
  }
}

}  // namespace

int main() {
  apptraverse::EnsureObjectRegistration();
  TestManualObjFacetRoundtrip();
  TestOrdinarySaveWithoutOriginFacet();
  TestDerivedWithOwnV1DoesNotWriteObjFacet();
  TestShouldSaveVersionBlocksEmptyOrigin();
  std::cout << "object_origin_facet_test OK\n";
  return 0;
}
