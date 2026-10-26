#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <utility>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj.h"
#include "aether-objects/obj/version_iterator.h"

#include "apptraverse/object_macros.h"

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

APPTRAVERSE_REGISTER(OriginFacetTargetV0);
APPTRAVERSE_REGISTER(OriginFacetTargetV1);
APPTRAVERSE_REGISTER(OriginFacetTargetV1Gated);

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
  if (HasStoredLayer(storage, instance_id, ae::Obj::kClassId, 1)) {
    Fail("Obj/S1 written implicitly by Save<DerivedWithV1> without origin");
  }
}

void TestAutoObjFacetOnSaveRootWithOrigin() {
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
  if (!HasStoredLayer(storage, instance_id, ae::Obj::kClassId, 1)) {
    Fail("missing auto Obj/S1 after Save with origin");
  }
  if (HasStoredLayer(storage, instance_id, OriginFacetTargetV0::kClassId, 1)) {
    Fail("spurious Derived/S1");
  }
  if (HasStoredLayer(storage, prefab_id, ae::Obj::kClassId, 0)) {
    Fail("prefab body written to USER");
  }
}

void TestDerivedS0S1PlusAutoObjFacetWithOrigin() {
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
  if (!HasStoredLayer(storage, instance_id, ae::Obj::kClassId, 1)) {
    Fail("missing auto Obj/S1 alongside Derived S1");
  }
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

void TestShouldSaveOriginFacetBlocksEmptyOrigin() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  ae::ObjId const instance_id{500u};

  OriginFacetTargetV0::ptr instance = OriginFacetTargetV0::ptr::Create(
      ae::CreateWith{domain}.with_id(instance_id));
  ae::Obj const& obj = *instance.Load();
  if (obj.ShouldSaveOriginFacet(ae::Version<1>{})) {
    Fail("ShouldSaveOriginFacet true without origin");
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

void TestColdLoadRootAppliesObjFacetOnDerived() {
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
    if (!HasStoredLayer(storage, instance_id, ae::Obj::kClassId, 1)) {
      Fail("auto Obj/S1 missing before cold load");
    }
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

void TestColdLoadWithoutOriginFacet() {
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
                     Fail("cold load without origin facet");
                   }
                   if (loaded->origin_id().is_valid()) {
                     Fail("unexpected origin after load without facet");
                   }
                 });
}

void TestColdLoadObjS0WithoutObjS1() {
  ae::RamDomainStorage storage;
  ae::ObjId const instance_id{603u};
  {
    ae::Domain domain{storage};
    OriginFacetTargetV0::ptr instance = OriginFacetTargetV0::ptr::Create(
        ae::CreateWith{domain}.with_id(instance_id));
    instance.Load()->value = 3;
    ae::DomainGraph graph{&domain};
    if (!graph.Save(*instance.Load(), instance_id).has_value()) {
      Fail("Save for Obj/S0-only case");
    }
  }
  WithColdLoadV0(storage, instance_id,
                 [](ae::Domain&, ae::DomainGraph&,
                    ae::Ptr<OriginFacetTargetV0> loaded) {
                   if (loaded->value != 3) {
                     Fail("cold load with Obj/S0 only");
                   }
                 });
}

void TestColdLoadDerivedS1WithObjOriginFacet() {
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
    if (!HasStoredLayer(storage, instance_id, ae::Obj::kClassId, 1)) {
      Fail("auto Obj/S1 missing alongside Derived/S1");
    }
    (void)obj;
  }
  WithColdLoadV1(storage, instance_id,
                 [prefab_id](ae::Domain&, ae::DomainGraph&,
                             ae::Ptr<OriginFacetTargetV1> loaded) {
                   if (loaded->value != 5 || loaded->tag != 6 ||
                       loaded->origin_id() != prefab_id) {
                     Fail("Derived S0/S1 + Obj/S1 cold load mismatch");
                   }
                 });
}

bool PrefabBodyAbsent(ae::RamDomainStorage const& storage, ae::ObjId prefab_id) {
  auto const it = storage.state.find(prefab_id);
  if (it == storage.state.end() || !it->second.has_value()) {
    return true;
  }
  return it->second->empty();
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
    if (!HasStoredLayer(storage, A_id, OriginFacetTargetV0::kClassId, 0) ||
        !HasStoredLayer(storage, A_id, ae::Obj::kClassId, 1)) {
      Fail("SaveRoot missing Derived/S0 or Obj/S1");
    }
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
    if (HasStoredLayer(storage, no_origin_id, ae::Obj::kClassId, 1)) {
      Fail("Obj/S1 without origin on SaveRoot");
    }
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
        !HasStoredLayer(storage, A_id, OriginFacetTargetV1::kClassId, 1) ||
        !HasStoredLayer(storage, A_id, ae::Obj::kClassId, 1)) {
      Fail("missing Derived S0/S1 or Obj/S1");
    }
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
    if (!HasStoredLayer(storage, gated_id, ae::Obj::kClassId, 1)) {
      Fail("gated missing auto Obj/S1 with origin");
    }
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

void TestLoadRootRejectsObjFacetOnlyStorage() {
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
    if (!graph.SaveVersion(ae::Version<1>{}, static_cast<ae::Obj const&>(obj),
                           instance_id)
             .IsOk()) {
      Fail("write Obj/S1-only storage");
    }
    domain.RemoveObject(&obj);
  }
  ae::Domain domain{storage};
  ae::DomainGraph graph{&domain};
  if (graph.LoadRoot(instance_id)) {
    Fail("LoadRoot must fail for Obj/S1-only storage");
  }
}

}  // namespace

int main() {
  apptraverse::EnsureObjectRegistration();
  (void)&g_apptraverse_registrar_OriginFacetTargetV0;
  (void)&g_apptraverse_registrar_OriginFacetTargetV1;
  (void)&g_apptraverse_registrar_OriginFacetTargetV1Gated;
  TestManualObjFacetRoundtrip();
  TestOrdinarySaveWithoutOriginFacet();
  TestDerivedWithOwnV1DoesNotWriteObjFacetWithoutOrigin();
  TestAutoObjFacetOnSaveRootWithOrigin();
  TestDerivedS0S1PlusAutoObjFacetWithOrigin();
  TestRepeatSaveRootDoesNotAddExtraLayers();
  TestShouldSaveOriginFacetBlocksEmptyOrigin();
  TestColdLoadRootAppliesObjFacetOnDerived();
  TestColdLoadWithoutOriginFacet();
  TestColdLoadObjS0WithoutObjS1();
  TestColdLoadDerivedS1WithObjOriginFacet();
  TestLoadRootRejectsObjFacetOnlyStorage();
  TestFinalOriginRoundtripV0();
  TestFinalOriginRoundtripV1WithShouldSaveVersion();
  std::cout << "object_origin_facet_test OK\n";
  return 0;
}
