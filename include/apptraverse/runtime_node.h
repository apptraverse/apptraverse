#ifndef APPTRAVERSE_RUNTIME_NODE_H_
#define APPTRAVERSE_RUNTIME_NODE_H_

#include <cassert>

#include "aether-objects/obj/obj.h"
#include "aether-objects/obj/obj_ptr.h"
#include "aether-objects/obj/registry.h"

#include "apptraverse/node.h"
#include "apptraverse/object_serialization.h"

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
  // Domain::Find returns the canonical owning Ptr for the most-derived
  // storage. Do not MakePtrFromThis(static_cast<Node*>(raw.get())) — that
  // reconstructs PtrStorage from a base subobject address (UB).
  auto held = node.domain->Find(id);
  assert(held && "AddObject must make the base findable");
  node.base = Node::ptr{node.domain, id, {}, std::move(held)};
  node.CaptureBaseState();
  assert(node.base.is_valid());
  assert(node.journal.empty());
}

// Same as InitializeRuntimeNode, then inherit the model-runtime notifier from
// an already-bound Node (e.g. parent Surface) so Events on the new Node notify
// the same runtime without session-specific Surface branching.
inline void InitializeRuntimeNode(Node& node, Node const& runtime_source) {
  InitializeRuntimeNode(node);
  node.CopyMaterializedChangeNotifierFrom(runtime_source);
  assert(node.base.is_valid());
  assert(node.base.is_loaded());
  // Node::base is a historical snapshot, never a live participant. It is
  // replaced by CaptureBaseState / CompactJournal and dropped on the GUI side
  // by FinalizeUiNodeState, so a notifier bound to it would hand the runtime
  // a pointer the owning Node is free to discard. Only live Nodes are bound.
  assert(!node.base->HasMaterializedChangeNotifier());
}

inline void BindReachableNodesMaterializedChangeNotifier(
    ae::Obj& root, void* ctx, Node::MaterializedChangeFn fn) {
  std::vector<Node*> nodes;
  CollectReachableNodes(root, nodes);
  for (Node* node : nodes) {
    node->BindMaterializedChangeNotifier(ctx, fn);
  }
}

inline void ClearReachableNodesMaterializedChangeNotifier(ae::Obj& root) {
  std::vector<Node*> nodes;
  CollectReachableNodes(root, nodes);
  for (Node* node : nodes) {
    node->ClearMaterializedChangeNotifier();
  }
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_RUNTIME_NODE_H_
