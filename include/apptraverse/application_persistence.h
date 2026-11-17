#ifndef APPTRAVERSE_APPLICATION_PERSISTENCE_H_
#define APPTRAVERSE_APPLICATION_PERSISTENCE_H_
#include <vector>
#include "apptraverse/object_serialization.h"
namespace apptraverse {
// Optional accepted-lifecycle hook for applications that persist USER overlays.
// RAM-only applications do not call it. Each node keeps its runtime retention
// policy.
inline void CompactApplicationNodesForUserOverlay(Node& root) {
  std::vector<ae::Ptr<ae::Obj>> nodes;
  CollectReachableNodeRefs(root, nodes);
  auto const now = SystemUtcMicros();
  for (auto const& object : nodes) {
    auto& node = static_cast<Node&>(*object);
    auto const retention = node.GetJournalRetentionPolicy();
    node.SetJournalRetentionPolicy({.max_events = 0});
    node.CompactJournal(now);
    node.SetJournalRetentionPolicy(retention);
  }
}
}  // namespace apptraverse
#endif
