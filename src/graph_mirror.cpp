#include "apptraverse/graph_mirror.h"

#include <cassert>

#include "aether-objects/obj/registry.h"

#include "apptraverse/node.h"
#include "apptraverse/object_serialization.h"

namespace apptraverse {
namespace {

ae::Ptr<ae::Obj> CreateUiShell(ae::Domain& ui_domain, ae::Obj const& model) {
  if (auto existing = ui_domain.Find(model.obj_id)) {
    return existing;
  }
  auto* factory =
      ae::Registry::GetRegistry().FindFactory(model.GetClassId());
  assert(factory != nullptr);
  assert(factory->create != nullptr);
  ae::Ptr<ae::Obj> raw = factory->create();
  raw->domain = &ui_domain;
  raw->obj_id = model.obj_id;
  ui_domain.AddObject(model.obj_id, raw);
  return raw;
}

std::uint64_t ObjectGeneration(ae::Obj const& object) {
  if (auto const* node = dynamic_cast<Node const*>(&object)) {
    return node->Generation();
  }
  return 1;
}

}  // namespace

ae::Ptr<ae::Obj> CopyModelGraphToUiDomain(ae::Obj& model_root,
                                          ae::Domain& ui_domain,
                                          ae::IDomainStorage& ui_storage) {
  std::vector<Node*> model_nodes;
  CollectReachableNodes(model_root, model_nodes);
  for (Node* node : model_nodes) {
    node->EnsureCurrentGeneration();
  }

  std::vector<ae::Obj*> objects;
  CollectReachableObjects(model_root, objects);

  std::vector<ae::Ptr<ae::Obj>> keepalive;
  keepalive.reserve(objects.size());
  for (ae::Obj* object : objects) {
    keepalive.push_back(CreateUiShell(ui_domain, *object));
  }

  for (ae::Obj* object : objects) {
    auto ui_object = ui_domain.Find(object->obj_id);
    assert(ui_object);

    ByteSink scratch;
    SerializeObjectToBuffer(*object, scratch);

    ByteSource payload;
    payload.data = scratch.bytes.data();
    payload.size = scratch.bytes.size();
    DeserializeObjectFromBuffer(*ui_object, payload, ui_domain, ui_storage);
    FinalizeUiNodeState(*ui_object, ObjectGeneration(*object));
  }

  auto ui_root = ui_domain.Find(model_root.obj_id);
  assert(ui_root);
  // Domain holds weak refs only; the mirrored ObjPtr graph must keep nodes alive
  // once keepalive is released. The returned root Ptr is the caller's anchor.
  keepalive.clear();
  ui_root = ui_domain.Find(model_root.obj_id);
  assert(ui_root && "UI mirror graph must stay reachable via ObjPtr refs");
  return ui_root;
}

}  // namespace apptraverse
