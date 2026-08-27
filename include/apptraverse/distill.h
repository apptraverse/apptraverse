#ifndef APPTRAVERSE_DISTILL_H_
#define APPTRAVERSE_DISTILL_H_

#include <cassert>
#include <vector>

#include "aether/obj/domain.h"
#include "aether/obj/obj.h"
#include "aether/obj/obj_ptr.h"
#include "aether/obj/registry.h"

#include "apptraverse/object_serialization.h"

namespace apptraverse {

// Walk every Node reachable from root, create a same-class base object, assign
// Node::base, and capture base state. Application code does not choose base ids.
inline void FinalizeDistilledGraph(ae::Obj& application_root) {
  std::vector<Node*> nodes;
  CollectReachableNodes(application_root, nodes);
  for (Node* node : nodes) {
    if (node->base) {
      continue;
    }
    auto* factory =
        ae::Registry::GetRegistry().FindFactory(node->GetClassId());
    assert(factory != nullptr);
    assert(factory->create != nullptr);
    ae::Ptr<ae::Obj> raw = factory->create();
    ae::ObjId const id = ae::ObjId::GenerateUnique();
    raw->domain = node->domain;
    raw->obj_id = id;
    node->domain->AddObject(id, raw);
    node->base = Node::ptr::MakeFromThis(static_cast<Node*>(raw.get()));
    node->CaptureBaseState();
  }
}

template <typename T>
void SaveDistilledRoot(T& root) {
  T::ptr::MakeFromThis(&root).Save();  // runtime-save-ok: distill
}

template <typename T>
typename T::ptr LoadApplication(ae::Domain& domain, ae::ObjId id) {
  auto root = T::ptr::Declare(ae::CreateWith{domain}.with_id(id));
  root.Load();
  assert(root);
  return root;
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_DISTILL_H_
