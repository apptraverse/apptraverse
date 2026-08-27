#ifndef APPTRAVERSE_RUNTIME_NODE_H_
#define APPTRAVERSE_RUNTIME_NODE_H_

#include <cassert>

#include "aether/obj/obj.h"
#include "aether/obj/obj_ptr.h"
#include "aether/obj/registry.h"

#include "apptraverse/node.h"

namespace apptraverse {

// Make a newly created Node a valid AppTraverse Node: same-class base object,
// CaptureBaseState(), empty journal, initial Generation. Does not register with
// ModelRuntime or presentation roots — use ModelRuntime::AttachNode for that.
inline void InitializeRuntimeNode(Node& node) {
  assert(node.domain != nullptr);
  assert(!node.base.is_valid());
  assert(node.journal.empty());

  auto* factory = ae::Registry::GetRegistry().FindFactory(node.GetClassId());
  assert(factory != nullptr);
  assert(factory->create != nullptr);
  ae::Ptr<ae::Obj> raw = factory->create();
  ae::ObjId const id = ae::ObjId::GenerateUnique();
  raw->domain = node.domain;
  raw->obj_id = id;
  node.domain->AddObject(id, raw);
  node.base = Node::ptr::MakeFromThis(static_cast<Node*>(raw.get()));
  node.CaptureBaseState();
  assert(node.base.is_valid());
  assert(node.journal.empty());
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_RUNTIME_NODE_H_
