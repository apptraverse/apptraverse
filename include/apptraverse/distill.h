#ifndef APPTRAVERSE_DISTILL_H_
#define APPTRAVERSE_DISTILL_H_

#include <cstdio>
#include <cstdlib>
#include <vector>

#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj.h"

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/runtime_node.h"

namespace apptraverse {

// Walk every Node reachable from root and InitializeRuntimeNode each one that
// still lacks a base. Application code does not choose base ids.
inline void FinalizeDistilledGraph(ae::Obj& application_root) {
  std::vector<Node*> nodes;
  CollectReachableNodes(application_root, nodes);
  for (Node* node : nodes) {
    if (node->base.is_valid()) {
      continue;
    }
    if (!node->journal.empty()) {
      continue;
    }
    InitializeRuntimeNode(*node);
  }
}

// Normalize generations/journals so repeated distill runs emit identical bytes.
inline void StabilizeDistilledPersistentSnapshot(ae::Obj& application_root) {
  std::vector<Node*> nodes;
  CollectReachableNodes(application_root, nodes);
  for (Node* node : nodes) {
    node->journal.clear();
    if (node->base.is_valid()) {
      node->CaptureBaseState();
    }
    node->AdoptPublishedGeneration(1);
  }
}

template <typename T>
void SaveDistilledRoot(T& root) {
  ae::DomainGraph graph{root.domain, ae::GraphSerializationScope::LocalPersistent};
  graph.force_materialize = true;
  typename T::ptr const holder = T::ptr::MakeFromThis(&root);
  ae::Ptr<ae::Obj> const obj = holder.cached();
  if (!obj) {
    WriteFatalStderr("fatal: SaveDistilledRoot root not cached\n");
    std::abort();
  }
  ae::seri::SeriResult const result = graph.SaveRoot(obj, root.obj_id);
  if (result.IsErr()) {
    WriteFatalStderr("fatal: SaveDistilledRoot failed\n");
    std::abort();
  }
}

template <typename T>
typename T::ptr LoadApplication(ae::Domain& domain, ae::ObjId id) {
  auto root = T::ptr::Declare(ae::CreateWith{domain}.with_id(id));
  root.Load();
  {
    ae::DomainGraph flush_graph{&domain};
    flush_graph.FlushIncompleteStoredLayers();
  }
  if (!root) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "fatal: LoadApplication failed id=%u\n",
                  id.id());
    WriteFatalStderr(buf);
    std::abort();
  }
  return root;
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_DISTILL_H_
