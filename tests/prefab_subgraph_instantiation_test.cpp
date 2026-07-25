#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>

#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/obj/obj.h"

#include "apptraverse/node_for.h"

namespace apptraverse::test {

class SharedResource : public ae::Obj {
  AE_OBJECT(SharedResource, Obj, 0)

 protected:
  SharedResource() = default;

 public:
  explicit SharedResource(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(value))

  std::string value;
};

static_assert(!std::is_base_of_v<apptraverse::Node, SharedResource>);

class PrefabNode;

class OwnedObject : public ae::Obj {
  AE_OBJECT(OwnedObject, Obj, 0)

 protected:
  OwnedObject() = default;

 public:
  explicit OwnedObject(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(caption), AE_MMBR(owner))

  std::string caption;
  ae::ObjPtr<PrefabNode> owner;
};

static_assert(!std::is_base_of_v<apptraverse::Node, OwnedObject>);

class PrefabNode : public apptraverse::NodeFor<PrefabNode> {
  AE_OBJECT(PrefabNode, Node, 0)

 protected:
  PrefabNode() = default;

 public:
  explicit PrefabNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(name), AE_MMBR(owned), AE_MMBR(resource))

  std::string name;
  OwnedObject::ptr owned;
  SharedResource::ptr resource;

  void CapturePrefabBaseForTest() { CaptureBaseState(); }

  PrefabNode::ptr Instantiate(std::string instance_name);
};

PrefabNode::ptr PrefabNode::Instantiate(std::string instance_name) {
  assert(domain != nullptr);
  assert(obj_id.IsValid());
  assert(base.is_valid());
  assert(base.is_loaded());
  assert(owned.is_valid());
  assert(owned.is_loaded());
  assert(journal.empty());

  auto prefab = PrefabNode::ptr::MakeFromThis(this);
  auto instance = prefab.Clone();
  auto instance_base = base.Clone();
  auto instance_owned = owned.Clone();

  assert(instance.is_valid());
  assert(instance.is_loaded());
  assert(instance_base.is_valid());
  assert(instance_base.is_loaded());
  assert(instance_owned.is_valid());
  assert(instance_owned.is_loaded());
  assert(instance.id() != prefab.id());
  assert(instance_base.id() != base.id());
  assert(instance_owned.id() != owned.id());

  instance->base = instance_base;
  instance->owned = instance_owned;
  instance_owned->owner = instance;

  instance->name = std::move(instance_name);
  assert(instance->journal.empty());

  instance->CaptureBaseState();
  return instance;
}

class PrefabContext : public ae::Obj {
  AE_OBJECT(PrefabContext, Obj, 0)

 protected:
  PrefabContext() = default;

 public:
  explicit PrefabContext(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(node_prefab))

  PrefabNode::ptr node_prefab;

  PrefabNode::ptr CreateInstance(std::string name) {
    assert(node_prefab.is_valid());
    assert(node_prefab.is_loaded());
    return node_prefab->Instantiate(std::move(name));
  }
};

static_assert(!std::is_base_of_v<apptraverse::Node, PrefabContext>);

}  // namespace apptraverse::test

namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " (" << __FILE__ << ':'         \
                << __LINE__ << ")\n";                                        \
      return EXIT_FAILURE;                                                   \
    }                                                                        \
  } while (0)

bool ContainsObj(ae::RamDomainStorage const& storage, ae::ObjId::Type id) {
  return storage.state.find(ae::ObjId{id}) != storage.state.end();
}

}  // namespace

int main() {
  using apptraverse::test::OwnedObject;
  using apptraverse::test::PrefabContext;
  using apptraverse::test::PrefabNode;
  using apptraverse::test::SharedResource;

  ae::RamDomainStorage storage;
  ae::Domain distillation_domain{ae::Now(), storage};
  CHECK(storage.state.empty());

  SharedResource::ptr resource = SharedResource::ptr::Create(
      ae::CreateWith{distillation_domain}.with_id(50));
  CHECK(static_cast<bool>(resource));
  resource->value = "Default shared resource";

  OwnedObject::ptr owned_prefab = OwnedObject::ptr::Create(
      ae::CreateWith{distillation_domain}.with_id(12));
  CHECK(static_cast<bool>(owned_prefab));
  owned_prefab->caption = "Owned object";

  PrefabNode::ptr node_base_prefab =
      PrefabNode::ptr::Create(ae::CreateWith{distillation_domain}.with_id(11));
  CHECK(static_cast<bool>(node_base_prefab));
  node_base_prefab->name = "";
  CHECK(!node_base_prefab->base.is_valid());
  CHECK(node_base_prefab->journal.empty());
  CHECK(!node_base_prefab->owned.is_valid());
  CHECK(!node_base_prefab->resource.is_valid());

  PrefabNode::ptr node_prefab =
      PrefabNode::ptr::Create(ae::CreateWith{distillation_domain}.with_id(10));
  CHECK(static_cast<bool>(node_prefab));
  node_prefab->name = "";
  node_prefab->base = node_base_prefab;
  node_prefab->owned = owned_prefab;
  node_prefab->resource = resource;
  owned_prefab->owner = node_prefab;

  CHECK(node_prefab->base.Load().get() == node_base_prefab.Load().get());
  CHECK(node_prefab->owned.Load().get() == owned_prefab.Load().get());
  CHECK(node_prefab->resource.Load().get() == resource.Load().get());
  CHECK(owned_prefab->owner.Load().get() == node_prefab.Load().get());

  node_prefab->CapturePrefabBaseForTest();

  CHECK(storage.state.size() == 3);
  CHECK(ContainsObj(storage, 11));
  CHECK(ContainsObj(storage, 12));
  CHECK(ContainsObj(storage, 50));
  CHECK(!ContainsObj(storage, 1));
  CHECK(!ContainsObj(storage, 10));

  auto* captured_base = node_prefab->base.Load().as<PrefabNode>();
  CHECK(captured_base != nullptr);
  CHECK(captured_base->name.empty());
  CHECK(!captured_base->base.is_valid());
  CHECK(captured_base->journal.empty());
  CHECK(captured_base->owned.id().id() == 12);
  CHECK(captured_base->owned.Load().get() == owned_prefab.Load().get());
  CHECK(captured_base->resource.id().id() == 50);
  CHECK(captured_base->resource.Load().get() == resource.Load().get());

  CHECK(owned_prefab->owner.id().id() == 10);
  CHECK(owned_prefab->owner.Load().get() == node_prefab.Load().get());
  CHECK(owned_prefab->owner.Load().get() != node_base_prefab.Load().get());

  PrefabContext::ptr context =
      PrefabContext::ptr::Create(ae::CreateWith{distillation_domain}.with_id(1));
  CHECK(static_cast<bool>(context));
  context->node_prefab = node_prefab;
  context.Save();

  CHECK(storage.state.size() == 5);
  CHECK(ContainsObj(storage, 1));
  CHECK(ContainsObj(storage, 10));
  CHECK(ContainsObj(storage, 11));
  CHECK(ContainsObj(storage, 12));
  CHECK(ContainsObj(storage, 50));

  ae::Domain runtime_domain{ae::Now(), storage};
  PrefabContext::ptr loaded_context =
      PrefabContext::ptr::Declare(ae::CreateWith{runtime_domain}.with_id(1));
  loaded_context.Load();

  CHECK(loaded_context.is_loaded());
  CHECK(loaded_context->node_prefab.is_loaded());
  CHECK(loaded_context->node_prefab.id().id() == 10);
  CHECK(loaded_context->node_prefab->GetClassId() == PrefabNode::kClassId);
  CHECK(loaded_context->node_prefab->base.id().id() == 11);
  CHECK(loaded_context->node_prefab->base.is_loaded());
  CHECK(loaded_context->node_prefab->base.Load().as<PrefabNode>() != nullptr);
  CHECK(loaded_context->node_prefab->owned.id().id() == 12);
  CHECK(loaded_context->node_prefab->owned.is_loaded());
  CHECK(loaded_context->node_prefab->resource.id().id() == 50);
  CHECK(loaded_context->node_prefab->resource.is_loaded());
  CHECK(loaded_context->node_prefab->owned->owner.Load().get() ==
        loaded_context->node_prefab.Load().get());

  auto* loaded_prefab_base =
      loaded_context->node_prefab->base.Load().as<PrefabNode>();
  CHECK(loaded_prefab_base != nullptr);
  CHECK(loaded_prefab_base->owned.Load().get() ==
        loaded_context->node_prefab->owned.Load().get());
  CHECK(loaded_prefab_base->resource.Load().get() ==
        loaded_context->node_prefab->resource.Load().get());
  CHECK(loaded_context->node_prefab->journal.empty());
  CHECK(loaded_prefab_base->journal.empty());

  auto* prefab_address = loaded_context->node_prefab.Load().get();
  auto* prefab_base_address =
      loaded_context->node_prefab->base.Load().get();
  auto* prefab_owned_address =
      loaded_context->node_prefab->owned.Load().get();
  auto* resource_address =
      loaded_context->node_prefab->resource.Load().get();
  CHECK(prefab_address != nullptr);
  CHECK(prefab_base_address != nullptr);
  CHECK(prefab_owned_address != nullptr);
  CHECK(resource_address != nullptr);

  auto first = loaded_context->CreateInstance("First");
  auto second = loaded_context->CreateInstance("Second");

  CHECK(first.is_valid());
  CHECK(first.is_loaded());
  CHECK(second.is_valid());
  CHECK(second.is_loaded());
  CHECK(first.id() != second.id());
  CHECK(first.id().id() != 10);
  CHECK(second.id().id() != 10);
  CHECK(first->base.id() != second->base.id());
  CHECK(first->base.id().id() != 11);
  CHECK(second->base.id().id() != 11);
  CHECK(first->owned.id() != second->owned.id());
  CHECK(first->owned.id().id() != 12);
  CHECK(second->owned.id().id() != 12);

  CHECK(first.Load().get() != second.Load().get());
  CHECK(first.Load().get() != prefab_address);
  CHECK(second.Load().get() != prefab_address);
  CHECK(first->base.Load().get() != second->base.Load().get());
  CHECK(first->base.Load().get() != prefab_base_address);
  CHECK(second->base.Load().get() != prefab_base_address);
  CHECK(first->owned.Load().get() != second->owned.Load().get());
  CHECK(first->owned.Load().get() != prefab_owned_address);
  CHECK(second->owned.Load().get() != prefab_owned_address);

  CHECK(first->name == "First");
  CHECK(first->journal.empty());
  CHECK(first->owned.is_valid());
  CHECK(first->owned.is_loaded());
  CHECK(first->owned->caption == "Owned object");
  CHECK(first->owned->owner.id() == first.id());
  CHECK(first->owned->owner.Load().get() == first.Load().get());

  CHECK(first->base.is_valid());
  CHECK(first->base.is_loaded());
  auto* first_base = first->base.Load().as<PrefabNode>();
  CHECK(first_base != nullptr);
  CHECK(first_base->name == "First");
  CHECK(!first_base->base.is_valid());
  CHECK(first_base->journal.empty());
  CHECK(first_base->owned.id() == first->owned.id());
  CHECK(first_base->owned.Load().get() == first->owned.Load().get());
  CHECK(first->resource.id().id() == 50);
  CHECK(first->resource.Load().get() == resource_address);
  CHECK(first_base->resource.id().id() == 50);
  CHECK(first_base->resource.Load().get() == first->resource.Load().get());

  CHECK(second->name == "Second");
  CHECK(second->journal.empty());
  CHECK(second->owned.is_valid());
  CHECK(second->owned.is_loaded());
  CHECK(second->owned->caption == "Owned object");
  CHECK(second->owned->owner.id() == second.id());
  CHECK(second->owned->owner.Load().get() == second.Load().get());

  auto* second_base = second->base.Load().as<PrefabNode>();
  CHECK(second_base != nullptr);
  CHECK(second_base->name == "Second");
  CHECK(!second_base->base.is_valid());
  CHECK(second_base->journal.empty());
  CHECK(second_base->owned.id() == second->owned.id());
  CHECK(second_base->owned.Load().get() == second->owned.Load().get());
  CHECK(second->resource.id().id() == 50);
  CHECK(second->resource.Load().get() == resource_address);
  CHECK(second_base->resource.id().id() == 50);
  CHECK(second_base->resource.Load().get() == second->resource.Load().get());

  CHECK(first->resource.Load().get() == second->resource.Load().get());
  CHECK(first->resource.Load().get() ==
        loaded_context->node_prefab->resource.Load().get());
  CHECK(first->resource.id().id() == 50);
  CHECK(second->resource.id().id() == 50);
  CHECK(loaded_context->node_prefab->resource.id().id() == 50);

  CHECK(loaded_context->node_prefab.id().id() == 10);
  CHECK(loaded_context->node_prefab->name.empty());
  CHECK(loaded_context->node_prefab->base.id().id() == 11);
  CHECK(loaded_context->node_prefab->owned.id().id() == 12);
  CHECK(loaded_context->node_prefab->owned->owner.Load().get() ==
        prefab_address);
  CHECK(loaded_context->node_prefab->resource.id().id() == 50);
  CHECK(loaded_context->node_prefab->journal.empty());
  CHECK(loaded_prefab_base->name.empty());
  CHECK(loaded_prefab_base->journal.empty());
  CHECK(loaded_context->node_prefab.Load().get() == prefab_address);
  CHECK(loaded_context->node_prefab->base.Load().get() ==
        prefab_base_address);
  CHECK(loaded_context->node_prefab->owned.Load().get() ==
        prefab_owned_address);

  auto const first_id = first.id();
  auto const first_base_id = first->base.id();
  auto const first_owned_id = first->owned.id();
  auto const second_id = second.id();
  auto const second_base_id = second->base.id();
  auto const second_owned_id = second->owned.id();

  CHECK(storage.state.size() == 9);
  CHECK(ContainsObj(storage, first_base_id.id()));
  CHECK(ContainsObj(storage, first_owned_id.id()));
  CHECK(ContainsObj(storage, second_base_id.id()));
  CHECK(ContainsObj(storage, second_owned_id.id()));
  CHECK(!ContainsObj(storage, first_id.id()));
  CHECK(!ContainsObj(storage, second_id.id()));

  first.Save();
  second.Save();

  CHECK(storage.state.size() == 11);
  CHECK(ContainsObj(storage, first_id.id()));
  CHECK(ContainsObj(storage, second_id.id()));

  ae::Domain reload_domain{ae::Now(), storage};
  PrefabContext::ptr reloaded_context =
      PrefabContext::ptr::Declare(ae::CreateWith{reload_domain}.with_id(1));
  reloaded_context.Load();
  CHECK(reloaded_context.is_loaded());

  PrefabNode::ptr loaded_first =
      PrefabNode::ptr::Declare(ae::CreateWith{reload_domain}.with_id(first_id));
  loaded_first.Load();
  PrefabNode::ptr loaded_second =
      PrefabNode::ptr::Declare(ae::CreateWith{reload_domain}.with_id(second_id));
  loaded_second.Load();

  CHECK(loaded_first.is_loaded());
  CHECK(loaded_second.is_loaded());
  CHECK(loaded_first->name == "First");
  CHECK(loaded_second->name == "Second");

  auto* loaded_first_base = loaded_first->base.Load().as<PrefabNode>();
  auto* loaded_second_base = loaded_second->base.Load().as<PrefabNode>();
  CHECK(loaded_first_base != nullptr);
  CHECK(loaded_second_base != nullptr);
  CHECK(loaded_first_base->name == "First");
  CHECK(loaded_second_base->name == "Second");
  CHECK(loaded_first_base->journal.empty());
  CHECK(loaded_second_base->journal.empty());
  CHECK(!loaded_first_base->base.is_valid());
  CHECK(!loaded_second_base->base.is_valid());

  CHECK(loaded_first->owned.is_loaded());
  CHECK(loaded_second->owned.is_loaded());
  CHECK(loaded_first->owned.id() == first_owned_id);
  CHECK(loaded_second->owned.id() == second_owned_id);
  CHECK(loaded_first->owned->owner.Load().get() == loaded_first.Load().get());
  CHECK(loaded_second->owned->owner.Load().get() ==
        loaded_second.Load().get());
  CHECK(loaded_first_base->owned.Load().get() ==
        loaded_first->owned.Load().get());
  CHECK(loaded_second_base->owned.Load().get() ==
        loaded_second->owned.Load().get());
  CHECK(loaded_first->resource.id().id() == 50);
  CHECK(loaded_second->resource.id().id() == 50);
  CHECK(loaded_first_base->resource.id().id() == 50);
  CHECK(loaded_second_base->resource.id().id() == 50);

  CHECK(loaded_first->owned.Load().get() != loaded_second->owned.Load().get());
  CHECK(loaded_first->base.Load().get() != loaded_second->base.Load().get());
  CHECK(loaded_first->resource.Load().get() ==
        loaded_second->resource.Load().get());
  CHECK(loaded_first->resource.Load().get() ==
        reloaded_context->node_prefab->resource.Load().get());
  CHECK(loaded_first.Load().get() != first.Load().get());
  CHECK(loaded_second.Load().get() != second.Load().get());

  CHECK(reloaded_context->node_prefab.id().id() == 10);
  CHECK(reloaded_context->node_prefab->base.id().id() == 11);
  CHECK(reloaded_context->node_prefab->owned.id().id() == 12);
  CHECK(reloaded_context->node_prefab->resource.id().id() == 50);
  CHECK(reloaded_context->node_prefab->owned->owner.Load().get() ==
        reloaded_context->node_prefab.Load().get());
  CHECK(reloaded_context->node_prefab->owned->owner.Load().get() !=
        loaded_first.Load().get());
  CHECK(reloaded_context->node_prefab->owned->owner.Load().get() !=
        loaded_second.Load().get());

  return EXIT_SUCCESS;
}
