#include <cstdlib>
#include <iostream>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj_id.h"
#include "aether-objects/obj/obj_ptr.h"

#include "apptraverse/object_macros.h"

namespace apptraverse::test {
namespace {

void Fail(char const* message) {
  std::cerr << "aether_domain_hierarchy_test FAIL: " << message << '\n';
  std::exit(1);
}

class HierarchyLeaf : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::HierarchyLeaf", HierarchyLeaf,
                           ae::Obj, 0)

 protected:
  HierarchyLeaf() = default;

 public:
  explicit HierarchyLeaf(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT()

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    (void)dnv;
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    (void)dnv;
  }
};

class HydrationOwner : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::HydrationOwner", HydrationOwner,
                           ae::Obj, 0)

 protected:
  HydrationOwner() = default;

 public:
  explicit HydrationOwner(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(reference), AE_MMBR(missing_reference),
                   AE_MMBR(invalid_reference))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, value, reference, missing_reference, invalid_reference);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, value, reference, missing_reference, invalid_reference);
  }

  int value{};
  HierarchyLeaf::ptr reference;
  HierarchyLeaf::ptr missing_reference;
  HierarchyLeaf::ptr invalid_reference;
};

APPTRAVERSE_REGISTER(HierarchyLeaf);
APPTRAVERSE_REGISTER(HydrationOwner);

void TestScopedLayerHydration() {
  ae::ObjId const owner_id{501u};
  ae::ObjId const leaf_id{502u};
  ae::ObjId const missing_id{503u};
  ae::RamDomainStorage target_storage;
  ae::RamDomainStorage snapshot_storage;
  ae::RamDomainStorage empty_storage;
  ae::Domain target{target_storage};
  ae::Domain snapshot{snapshot_storage};
  auto owner = HydrationOwner::ptr::Create(
      ae::CreateWith{target}.with_id(owner_id));
  auto snapshot_owner = HydrationOwner::ptr::Create(
      ae::CreateWith{snapshot}.with_id(owner_id));
  auto leaf = HierarchyLeaf::ptr::Create(
      ae::CreateWith{target}.with_id(leaf_id));
  auto* leaf_identity = leaf.Load().get();

  owner->value = 11;
  owner->reference = HierarchyLeaf::ptr::Declare(
      ae::CreateWith{target}.with_id(leaf_id));
  owner->missing_reference = HierarchyLeaf::ptr::Declare(
      ae::CreateWith{target}.with_id(missing_id));
  snapshot_owner->value = 22;
  snapshot_owner->reference = HierarchyLeaf::ptr::Declare(
      ae::CreateWith{snapshot}.with_id(leaf_id));
  snapshot_owner->missing_reference = HierarchyLeaf::ptr::Declare(
      ae::CreateWith{snapshot}.with_id(missing_id));
  auto target_save = ae::DomainGraph{&target}.SaveVersion(
      ae::Version<0>{}, *owner, owner_id);
  auto snapshot_save = ae::DomainGraph{&snapshot}.SaveVersion(
      ae::Version<0>{}, *snapshot_owner, owner_id);
  if (target_save.IsErr() || snapshot_save.IsErr()) {
    Fail("scoped hydration fixtures save");
  }

  // A serialized invalid reference must also clear a previously live cache.
  owner->invalid_reference = leaf;
  ae::DomainGraph hydration{&target};
  hydration.SetLoadStorage(&snapshot_storage);
  hydration.retain_loaded_references = true;
  auto hydrated = hydration.LoadVersion(ae::Version<0>{}, *owner, owner_id);
  if (hydrated.IsErr() || owner->value != 22 ||
      owner->reference.cached().get() != leaf_identity ||
      owner->reference.domain() != &target ||
      (owner->reference.flags() & ae::ObjFlags::kUnloaded) == 0) {
    Fail("snapshot layers bind retained lazy references to target identity");
  }
  if (owner->missing_reference.is_loaded() || target.Find(missing_id) ||
      owner->invalid_reference.is_valid() ||
      owner->invalid_reference.is_loaded()) {
    Fail("retention must not load absent or invalid references");
  }
  leaf.Reset();
  if (target.Find(leaf_id).get() != leaf_identity) {
    Fail("hydrated reference must own preconstructed snapshot child");
  }

  hydration.SetLoadStorage(&empty_storage);
  owner->value = 33;
  auto absent = hydration.LoadVersion(ae::Version<0>{}, *owner, owner_id);
  if (!ae::IsMissingVersion(absent) || owner->value != 33) {
    Fail("exclusive snapshot must not fall back to target layers");
  }

  // Another graph retains ordinary reads and lazy pointer semantics.
  ae::DomainGraph ordinary{&target};
  auto ordinary_load = ordinary.LoadVersion(ae::Version<0>{}, *owner, owner_id);
  if (ordinary_load.IsErr() || owner->value != 11 ||
      owner->reference.is_loaded() || target.Find(leaf_id)) {
    Fail("hydration options must not leak into another graph");
  }

  hydration.SetLoadStorage(nullptr);
  owner->value = 44;
  auto restored = hydration.LoadVersion(ae::Version<0>{}, *owner, owner_id);
  if (restored.IsErr() || owner->value != 11) {
    Fail("clearing layer storage restores domain reads");
  }
}

void TestAncestorLayerLookup() {
  ae::ObjId const owner_id{601u};
  ae::RamDomainStorage grandparent_storage;
  ae::RamDomainStorage parent_storage;
  ae::RamDomainStorage child_storage;
  ae::Domain grandparent{grandparent_storage};
  ae::Domain parent{parent_storage};
  ae::Domain child{child_storage};
  parent.set_parent_domain(&grandparent);
  child.set_parent_domain(&parent);

  auto stored = HydrationOwner::ptr::Create(
      ae::CreateWith{grandparent}.with_id(owner_id));
  stored->value = 17;
  auto ancestor_save = ae::DomainGraph{&grandparent}.SaveRoot(
      stored.Load(), owner_id);
  if (ancestor_save.IsErr()) {
    Fail("save grandparent layer fixture");
  }
  stored.Reset();
  if (grandparent.Find(owner_id)) {
    Fail("grandparent lookup fixture must require storage loading");
  }

  auto inherited = HydrationOwner::ptr::Declare(
      ae::CreateWith{child}.with_id(owner_id));
  ae::Ptr<HydrationOwner> inherited_object = inherited.Load();
  if (!inherited_object || inherited_object->value != 17 ||
      inherited_object->domain != &child ||
      !child.FindLocal(owner_id) || !parent_storage.state.empty()) {
    Fail("cold child loads grandparent layers through empty parent");
  }

  inherited_object->value = 29;
  auto local_save = ae::DomainGraph{&child}.SaveRoot(
      inherited_object, owner_id);
  if (local_save.IsErr()) {
    Fail("save local layer override");
  }
  inherited_object.Reset();
  inherited.Reset();
  auto local = inherited.Load();
  if (!local || local->value != 29 || local->domain != &child) {
    Fail("local stored layers shadow grandparent layers");
  }
  auto original = HydrationOwner::ptr::Declare(
      ae::CreateWith{grandparent}.with_id(owner_id));
  auto original_object = original.Load();
  if (!original_object || original_object->value != 17) {
    Fail("local layer override must not mutate ancestor storage");
  }
}


void TestColdLocalStorageShadowsLiveParent() {
  ae::RamDomainStorage parent_storage, child_storage;
  ae::Domain parent{parent_storage}, child{child_storage};
  child.set_parent_domain(&parent);
  ae::ObjId const id{701u};
  auto ancestor = HydrationOwner::ptr::Create(ae::CreateWith{parent}.with_id(id));
  ancestor->value = 17;
  auto local = HydrationOwner::ptr::Create(ae::CreateWith{child}.with_id(id));
  local->value = 29;
  auto save = ae::DomainGraph{&child}.SaveRoot(local.Load(), id);
  if (save.IsErr()) { Fail("save cold local shadow"); }
  local.Reset();
  if (child.FindLocal(id)) { Fail("cold local shadow must be unloaded"); }
  auto reloaded = local.Load();
  if (!reloaded || reloaded->value != 29 || reloaded->domain != &child ||
      reloaded.get() == ancestor.Load().get() || ancestor->value != 17) {
    Fail("local storage must shadow live ancestor during LoadRoot");
  }
}

int RunHierarchyTests() {
  using apptraverse::test::HierarchyLeaf;

  ae::RamDomainStorage parent_storage;
  ae::RamDomainStorage child_storage;
  ae::Domain parent{parent_storage};
  ae::Domain child{child_storage};
  child.set_parent_domain(&parent);

  ae::ObjId const parent_id{101u};
  ae::ObjId const child_only_id{202u};
  ae::ObjId const shared_shadow_id{303u};

  HierarchyLeaf::ptr parent_handle = HierarchyLeaf::ptr::Create(
      ae::CreateWith{parent}.with_id(parent_id));
  ae::Ptr<HierarchyLeaf> parent_obj = parent_handle.Load();

  auto parent_shadow_handle = HierarchyLeaf::ptr::Create(
      ae::CreateWith{parent}.with_id(shared_shadow_id));

  HierarchyLeaf::ptr child_shadow_handle = HierarchyLeaf::ptr::Create(
      ae::CreateWith{child}.with_id(shared_shadow_id));
  ae::Ptr<HierarchyLeaf> child_shadow = child_shadow_handle.Load();

  if (!parent.FindLocal(parent_id)) {
    Fail("parent FindLocal");
  }
  if (parent.FindLocal(child_only_id)) {
    Fail("parent should not see child-only id");
  }
  if (!child.Find(parent_id)) {
    Fail("child Find parent fallback");
  }
  if (!child.FindLocal(shared_shadow_id)) {
    Fail("child FindLocal shadow");
  }
  if (child.Find(shared_shadow_id).get() != child_shadow.get()) {
    Fail("local shadows parent with same id");
  }

  ae::RamDomainStorage sibling_storage;
  ae::Domain sibling{sibling_storage};
  sibling.set_parent_domain(&parent);
  auto sibling_handle = HierarchyLeaf::ptr::Create(
      ae::CreateWith{sibling}.with_id(child_only_id));
  auto sibling_shadow = HierarchyLeaf::ptr::Create(
      ae::CreateWith{sibling}.with_id(shared_shadow_id));
  if (!sibling.FindLocal(child_only_id) || parent.Find(child_only_id) ||
      child.Find(child_only_id) ||
      sibling.Find(shared_shadow_id).get() == child_shadow.get() ||
      parent.Find(shared_shadow_id).get() != parent_shadow_handle.Load().get()) {
    Fail("live sibling isolation and independent identical ids");
  }

  ae::ObjPtr<HierarchyLeaf> loaded{&child, parent_id, ae::ObjFlags{}, parent_obj};
  if (loaded.id() != parent_id) {
    Fail("loaded ObjPtr id");
  }
  if (loaded.domain() != &parent) {
    Fail("loaded ObjPtr domain follows object");
  }
  loaded.Reset();
  if (loaded.is_loaded()) {
    Fail("Reset unloads");
  }
  if (loaded.id() != parent_id) {
    Fail("unloaded ObjPtr retains id after Reset");
  }
  if (loaded.domain() != &parent) {
    Fail("unloaded ObjPtr retains domain after Reset");
  }

  loaded.Load();
  ae::ObjId const migrated_id{404u};
  parent.RemoveObject(parent_obj.get());
  child.AddObject(migrated_id, parent_obj);
  if (loaded.id() != migrated_id || loaded.domain() != &child ||
      parent.FindLocal(parent_id) || !child.FindLocal(migrated_id)) {
    Fail("loaded identity follows migrated object");
  }
  loaded.Reset();
  if (loaded.id() != migrated_id || loaded.domain() != &child ||
      !loaded.Load()) {
    Fail("reset retains migrated identity for reload");
  }

  TestScopedLayerHydration();
  TestAncestorLayerLookup();
  TestColdLocalStorageShadowsLiveParent();
  std::cout << "aether_domain_hierarchy_test OK\n";
  return 0;
}

}  // namespace

}  // namespace apptraverse::test

int main() { return apptraverse::test::RunHierarchyTests(); }
