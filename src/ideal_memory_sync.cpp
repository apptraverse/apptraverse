#include "apptraverse/ideal_memory_sync.h"

#include <cassert>
#include <utility>
#include <vector>

#include "apptraverse/shared_graph.h"

namespace apptraverse {
namespace {

bool StorageHasObject(ae::RamDomainStorage const& storage, ae::ObjId id) {
  auto const it = storage.state.find(id);
  return it != storage.state.end() && it->second.has_value();
}

}  // namespace

SyncResult SynchronizeSharedGraphOneWay(MemoryReplica& source,
                                        MemoryReplica& target) {
  assert(source.shared_root_id.IsValid());
  assert(target.shared_root_id == source.shared_root_id);

  auto source_root = Node::ptr::Declare(
      ae::CreateWith{source.domain}.with_id(source.shared_root_id));
  source_root.Load();
  assert(source_root.is_loaded());

  auto const discovered = DiscoverSharedGraph(source_root);
  assert(!discovered.empty());

  std::vector<ObjectState> missing_states;
  missing_states.reserve(discovered.size());
  for (auto const& source_node : discovered) {
    if (!StorageHasObject(target.storage, source_node.id())) {
      missing_states.push_back(CaptureNodeState(source_node, source.storage));
    }
  }

  SyncResult result;
  for (auto const& state : missing_states) {
    ImportObjectState(state, target.storage);
    ++result.nodes_imported;
  }

  for (auto const& source_node : discovered) {
    source_node.Load();
    assert(source_node.is_loaded());

    auto target_node = Node::ptr::Declare(
        ae::CreateWith{target.domain}.with_id(source_node.id()));
    target_node.Load();
    assert(target_node.is_loaded());

    bool node_changed = false;
    for (auto const& record : source_node->journal) {
      assert(record.event.is_valid());
      if (target_node->HasEvent(record.event.id())) {
        continue;
      }
      auto event = record.event;
      event.Load();
      assert(event.is_loaded());
      bool const accepted = TransferRemoteEvent(
          event, record.timestamp_us, source.storage, target_node,
          target.storage);
      assert(accepted);
      ++result.events_imported;
      node_changed = true;
    }
    if (node_changed) {
      target_node.Save();
    }
  }

  return result;
}

SyncResult SynchronizeSharedGraphBidirectional(MemoryReplica& left,
                                               MemoryReplica& right) {
  auto const a = SynchronizeSharedGraphOneWay(left, right);
  auto const b = SynchronizeSharedGraphOneWay(right, left);
  return SyncResult{
      a.nodes_imported + b.nodes_imported,
      a.events_imported + b.events_imported,
  };
}

}  // namespace apptraverse
