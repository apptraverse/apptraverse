#include "apptraverse/shared_graph_sync_session.h"

#include <algorithm>
#include <cassert>
#include <utility>

#include "aether/clock.h"

#include "apptraverse/shared_graph.h"

namespace apptraverse {
namespace {

std::vector<ae::ObjId> CollectJournalEventIds(Node::ptr node) {
  std::vector<ae::ObjId> ids;
  for (auto const& record : node->journal) {
    assert(record.event.is_valid());
    ids.push_back(record.event.id());
  }
  return ids;
}

struct JournalEntry {
  Node::ptr node;
  EventRecord record;
};

std::vector<JournalEntry> CollectOrderedJournalEntries(
    std::vector<Node::ptr> const& nodes) {
  std::vector<JournalEntry> entries;
  for (auto const& node : nodes) {
    node.Load();
    assert(node.is_loaded());
    for (auto const& record : node->journal) {
      entries.push_back(JournalEntry{node, record});
    }
  }
  std::sort(entries.begin(), entries.end(),
            [](JournalEntry const& a, JournalEntry const& b) {
              if (a.record.timestamp_us != b.record.timestamp_us) {
                return a.record.timestamp_us < b.record.timestamp_us;
              }
              return a.record.event.id() < b.record.event.id();
            });
  return entries;
}

}  // namespace

SharedGraphSyncSession::SharedGraphSyncSession(SyncReplica local_replica,
                                               SendFunction send)
    : local_{local_replica}, send_{std::move(send)} {
  assert(send_);
  assert(local_.shared_root_id.IsValid());
}

bool SharedGraphSyncSession::ContainsId(std::vector<ae::ObjId> const& ids,
                                        ae::ObjId id) const {
  for (auto const& existing : ids) {
    if (existing == id) {
      return true;
    }
  }
  return false;
}

void SharedGraphSyncSession::AddId(std::vector<ae::ObjId>& ids, ae::ObjId id) {
  if (!ContainsId(ids, id)) {
    ids.push_back(id);
  }
}

bool SharedGraphSyncSession::HasPendingEvent(ae::ObjId event_id) const {
  for (auto const& pending : pending_packets_) {
    if (pending.kind == PendingKind::kEvent && pending.event_id == event_id) {
      return true;
    }
  }
  return false;
}

bool SharedGraphSyncSession::IsEventCoveredByPendingNodeState(
    ae::ObjId event_id) const {
  for (auto const& pending : pending_packets_) {
    if (pending.kind != PendingKind::kNodeState) {
      continue;
    }
    if (ContainsId(pending.event_ids, event_id)) {
      return true;
    }
  }
  return false;
}

SharedGraphSyncSession::PendingPacket* SharedGraphSyncSession::FindPending(
    ae::ObjId packet_id) {
  for (auto& pending : pending_packets_) {
    if (pending.packet_id == packet_id) {
      return &pending;
    }
  }
  return nullptr;
}

bool SharedGraphSyncSession::StorageHasNode(ae::ObjId node_id) const {
  return StorageHasObject(local_.storage, node_id);
}

Node::ptr SharedGraphSyncSession::LoadLocalNode(ae::ObjId node_id) {
  auto node =
      Node::ptr::Declare(ae::CreateWith{local_.domain}.with_id(node_id));
  node.Load();
  assert(node.is_loaded());
  return node;
}

std::vector<ae::ObjId> SharedGraphSyncSession::CollectSharedGraphEventIds(
    Node::ptr root) {
  std::vector<ae::ObjId> ids;
  for (auto const& node : DiscoverSharedGraph(root)) {
    node.Load();
    assert(node.is_loaded());
    for (auto const& event_id : CollectJournalEventIds(node)) {
      AddId(ids, event_id);
    }
  }
  return ids;
}

void SharedGraphSyncSession::SendAck(ae::ObjId acknowledged_packet_id) {
  ae::RamDomainStorage build_storage;
  ae::Domain build_domain{ae::Now(), build_storage};
  auto ack = AckPacket::ptr::Create(ae::CreateWith{build_domain});
  ack->acknowledged_packet_id = acknowledged_packet_id;
  send_(SyncPacketCodec{}.Encode(ack));
}

void SharedGraphSyncSession::MarkReceivedAndAck(ae::ObjId packet_id) {
  AddId(successfully_received_packet_ids_, packet_id);
  SendAck(packet_id);
}

void SharedGraphSyncSession::SendInitialNodeState(Node::ptr root) {
  assert(root.is_valid());
  assert(root.is_loaded());
  assert(pending_packets_.empty());

  ae::RamDomainStorage build_storage;
  ae::Domain build_domain{ae::Now(), build_storage};
  CopyObjectGraph(root, local_.storage, build_domain, build_storage,
                  SharedCopyMode::kCopyLoadedTargets);
  auto build_node =
      Node::ptr::Declare(ae::CreateWith{build_domain}.with_id(root.id()));
  build_node.Load();
  assert(build_node.is_loaded());

  auto packet = NodeStatePacket::ptr::Create(ae::CreateWith{build_domain});
  packet->node = build_node;

  PendingPacket pending;
  pending.packet_id = packet.id();
  pending.bytes = SyncPacketCodec{}.Encode(packet);
  pending.kind = PendingKind::kNodeState;
  pending.node_id = root.id();
  pending.event_ids = CollectSharedGraphEventIds(root);
  pending.is_initial_state = true;

  send_(pending.bytes);
  pending_packets_.push_back(std::move(pending));
}

void SharedGraphSyncSession::SendEventPacket(Node::ptr node,
                                             EventRecord const& record) {
  assert(node.is_valid());
  assert(record.event.is_valid());
  auto event = record.event;
  event.Load();
  assert(event.is_loaded());
  assert(!HasPendingEvent(event.id()));

  ae::RamDomainStorage build_storage;
  ae::Domain build_domain{ae::Now(), build_storage};
  CopyObjectGraph(event, local_.storage, build_domain, build_storage,
                  SharedCopyMode::kCopyLoadedTargets);
  auto build_event =
      Event::ptr::Declare(ae::CreateWith{build_domain}.with_id(event.id()));
  build_event.Load();
  assert(build_event.is_loaded());

  auto packet = EventPacket::ptr::Create(ae::CreateWith{build_domain});
  packet->target_node_id = node.id();
  packet->timestamp_us = record.timestamp_us;
  packet->event = build_event;

  PendingPacket pending;
  pending.packet_id = packet.id();
  pending.bytes = SyncPacketCodec{}.Encode(packet);
  pending.kind = PendingKind::kEvent;
  pending.node_id = node.id();
  pending.event_id = event.id();

  send_(pending.bytes);
  pending_packets_.push_back(std::move(pending));
}

void SharedGraphSyncSession::StartInitialSynchronization() {
  assert(!initial_sync_started_);
  initial_sync_started_ = true;

  auto root = LoadLocalNode(local_.shared_root_id);
  SendInitialNodeState(root);
}

void SharedGraphSyncSession::Poll() {
  if (!initial_sync_complete_) {
    return;
  }

  auto root = LoadLocalNode(local_.shared_root_id);
  auto const discovered = DiscoverSharedGraph(root);
  for (auto const& node : discovered) {
    node.Load();
    assert(node.is_loaded());
    for (auto const& record : node->journal) {
      auto const event_id = record.event.id();
      if (ContainsId(delivered_event_ids_, event_id)) {
        continue;
      }
      if (IsEventCoveredByPendingNodeState(event_id)) {
        continue;
      }
      if (HasPendingEvent(event_id)) {
        continue;
      }
      SendEventPacket(node, record);
    }
  }
}

void SharedGraphSyncSession::RetryPending() {
  std::vector<SerializedSyncPacket> copies;
  copies.reserve(pending_packets_.size());
  for (auto const& pending : pending_packets_) {
    copies.push_back(pending.bytes);
  }
  for (auto& bytes : copies) {
    send_(std::move(bytes));
  }
}

void SharedGraphSyncSession::Receive(SerializedSyncPacket const& bytes) {
  auto decoded = SyncPacketCodec{}.Decode(bytes);
  assert(decoded.packet.is_loaded());
  auto const packet_id = decoded.packet.id();

  if (decoded.packet->GetClassId() == AckPacket::kClassId) {
    receiving_decoded_ = &decoded;
    decoded.packet->Dispatch(*this);
    receiving_decoded_ = nullptr;
    return;
  }

  if (ContainsId(successfully_received_packet_ids_, packet_id)) {
    SendAck(packet_id);
    return;
  }

  receiving_packet_id_ = packet_id;
  receiving_decoded_ = &decoded;
  decoded.packet->Dispatch(*this);
  receiving_decoded_ = nullptr;
  receiving_packet_id_ = {};
}

Event::ptr SharedGraphSyncSession::ImportEventForAccept(
    Event::ptr decoded_event, ae::IDomainStorage& decoded_storage) {
  auto imported = ImportObjectGraph(decoded_event, decoded_storage, local_,
                                    SharedCopyMode::kReferenceExistingTargets);
  auto event =
      Event::ptr::Declare(ae::CreateWith{local_.domain}.with_id(imported.id()));
  event.Load();
  assert(event.is_loaded());
  return event;
}

void SharedGraphSyncSession::MergeNodeStateGraph(
    NodeStatePacket const& packet, ae::IDomainStorage& decoded_storage) {
  assert(packet.node.is_valid());
  assert(packet.node.is_loaded());

  auto decoded_nodes = DiscoverSharedGraph(packet.node);
  // First pass: materialize missing shared Nodes (full graphs).
  for (auto const& decoded_node : decoded_nodes) {
    decoded_node.Load();
    assert(decoded_node.is_loaded());
    if (!StorageHasNode(decoded_node.id())) {
      ImportObjectGraph(decoded_node, decoded_storage, local_,
                        SharedCopyMode::kCopyLoadedTargets);
    }
  }

  // Second pass: apply missing Events in timestamp order.
  auto entries = CollectOrderedJournalEntries(decoded_nodes);
  for (auto const& entry : entries) {
    auto local_node = LoadLocalNode(entry.node.id());
    if (local_node->HasEvent(entry.record.event.id())) {
      AddId(delivered_event_ids_, entry.record.event.id());
      continue;
    }
    auto decoded_event = entry.record.event;
    decoded_event.Load();
    assert(decoded_event.is_loaded());
    auto imported =
        ImportEventForAccept(decoded_event, decoded_storage);
    auto const result = local_node->TryAcceptRemoteEvent(
        std::move(imported), entry.record.timestamp_us);
    assert(result != RemoteEventResult::kBlocked);
    if (result == RemoteEventResult::kAccepted) {
      local_node.Save();
    }
    AddId(delivered_event_ids_, entry.record.event.id());
  }
}

void SharedGraphSyncSession::Handle(NodeStatePacket const& packet) {
  assert(receiving_packet_id_.IsValid());
  assert(receiving_decoded_ != nullptr);
  MergeNodeStateGraph(packet, *receiving_decoded_->storage);
  MarkReceivedAndAck(receiving_packet_id_);
}

void SharedGraphSyncSession::Handle(EventPacket const& packet) {
  assert(receiving_packet_id_.IsValid());
  assert(receiving_decoded_ != nullptr);
  assert(packet.event.is_valid());
  assert(packet.event.is_loaded());

  if (!StorageHasNode(packet.target_node_id)) {
    // Target root missing — wait for initial NodeState; no ACK.
    return;
  }

  // Import missing shared Nodes referenced by the Event graph.
  ImportObjectGraph(packet.event, *receiving_decoded_->storage, local_,
                    SharedCopyMode::kReferenceExistingTargets);

  auto event = Event::ptr::Declare(
      ae::CreateWith{local_.domain}.with_id(packet.event.id()));
  event.Load();
  assert(event.is_loaded());

  auto target = LoadLocalNode(packet.target_node_id);
  auto const result =
      target->TryAcceptRemoteEvent(std::move(event), packet.timestamp_us);

  if (result == RemoteEventResult::kBlocked) {
    return;
  }

  if (result == RemoteEventResult::kAccepted) {
    target.Save();
  }
  AddId(delivered_event_ids_, packet.event.id());
  MarkReceivedAndAck(receiving_packet_id_);
}

void SharedGraphSyncSession::Handle(AckPacket const& packet) {
  auto* pending = FindPending(packet.acknowledged_packet_id);
  if (pending == nullptr) {
    return;
  }

  if (pending->kind == PendingKind::kNodeState) {
    for (auto const& event_id : pending->event_ids) {
      AddId(delivered_event_ids_, event_id);
    }
    if (pending->is_initial_state) {
      initial_sync_complete_ = true;
    }
  } else if (pending->kind == PendingKind::kEvent) {
    AddId(delivered_event_ids_, pending->event_id);
  }

  auto const packet_id = pending->packet_id;
  pending_packets_.erase(
      std::remove_if(pending_packets_.begin(), pending_packets_.end(),
                     [&](PendingPacket const& item) {
                       return item.packet_id == packet_id;
                     }),
      pending_packets_.end());
}

}  // namespace apptraverse
