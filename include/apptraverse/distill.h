#ifndef APPTRAVERSE_DISTILL_H_
#define APPTRAVERSE_DISTILL_H_

#include <cassert>
#include <vector>

#include "aether/obj/domain.h"
#include "aether/obj/obj.h"

#include "apptraverse/object_serialization.h"
#include "apptraverse/runtime_node.h"

namespace apptraverse {

// Walk every Node reachable from root and InitializeRuntimeNode each one that
// still lacks a base. Application code does not choose base ids.
inline void FinalizeDistilledGraph(ae::Obj& application_root) {
  std::vector<Node*> nodes;
  CollectReachableNodes(application_root, nodes);
  for (Node* node : nodes) {
    if (node->base) {
      continue;
    }
    InitializeRuntimeNode(*node);
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
