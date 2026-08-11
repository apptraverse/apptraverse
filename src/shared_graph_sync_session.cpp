#include "apptraverse/shared_graph_sync_session.h"

#include <algorithm>
#include <cassert>
#include <utility>

#include "apptraverse/shared_graph.h"

namespace apptraverse {
namespace {

bool StorageHasObject(ae::RamDomainStorage const& storage, ae::ObjId id) {
  auto const it = storage.state.find(id);
  return it != storage.state.end() && it->second.has_value();
}

std::vector<ae::ObjId> CollectJournalEventIds(Node::ptr node) {
  std::vector<ae::ObjId> ids;
  for (auto const& record : node->journal) {
    assert(record.event.is_valid());
    ids.push_back(record.event.id());
  }
  return ids;
}

}  // namespace

SharedGraphSyncSession::SharedGraphSyncSession(MemoryReplica local_replica,
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

bool SharedGraphSyncSession::HasPendingNodeState(ae::ObjId node_id) const {
  for (auto const& pending : pending_packets_) {
    if (pending.kind == PendingKind::kNodeState &&
        pending.node_id == node_id) {
      return true;
    }
  }
  return false;
}

bool SharedGraphSyncSession::HasPendingEvent(ae::ObjId event_id) const {
  for (auto const& pending : pending_packets_) {
    if (pending.kind == PendingKind::kEvent && pending.event_id == event_id) {
      return true;
    }
  }
  return false;
}

bool SharedGraphSyncSession::HasPendingNodeStateRequest(
    ae::ObjId node_id) const {
  for (auto const& pending : pending_packets_) {
    if (pending.kind == PendingKind::kNodeStateRequest &&
        pending.node_id == node_id) {
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

void SharedGraphSyncSession::SendAck(ae::ObjId acknowledged_packet_id) {
  auto ack = AckPacket::ptr::Create(ae::CreateWith{codec_.domain()});
  ack->acknowledged_packet_id = acknowledged_packet_id;
  ack.Save();
  send_(codec_.Encode(ack));
}

void SharedGraphSyncSession::MarkReceivedAndAck(ae::ObjId packet_id) {
  AddId(successfully_received_packet_ids_, packet_id);
  SendAck(packet_id);
}

void SharedGraphSyncSession::MaybeCompleteInitialSync() {
  if (!initial_sync_started_ || initial_sync_complete_) {
    return;
  }
  for (auto const& pending : pending_packets_) {
    if (pending.is_initial_state) {
      return;
    }
  }
  initial_sync_complete_ = true;
}

void SharedGraphSyncSession::SendNodeState(Node::ptr node, bool is_initial) {
  assert(node.is_valid());
  assert(node.is_loaded());
  assert(!HasPendingNodeState(node.id()));

  auto packet =
      NodeStatePacket::ptr::Create(ae::CreateWith{codec_.domain()});
  packet->state = CaptureNodeState(node, local_.storage);
  packet->required_nodes = DiscoverSharedDependencies(node);
  packet.Save();

  PendingPacket pending;
  pending.packet_id = packet.id();
  pending.bytes = codec_.Encode(packet);
  pending.kind = PendingKind::kNodeState;
  pending.node_id = node.id();
  pending.event_ids = CollectJournalEventIds(node);
  pending.is_initial_state = is_initial;

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

  auto packet = EventPacket::ptr::Create(ae::CreateWith{codec_.domain()});
  packet->target_node_id = node.id();
  packet->timestamp_us = record.timestamp_us;
  packet->state = CaptureEventState(event, local_.storage);
  packet->required_nodes = DiscoverSharedDependencies(event);
  packet.Save();

  PendingPacket pending;
  pending.packet_id = packet.id();
  pending.bytes = codec_.Encode(packet);
  pending.kind = PendingKind::kEvent;
  pending.node_id = node.id();
  pending.event_id = event.id();

  send_(pending.bytes);
  pending_packets_.push_back(std::move(pending));
}

void SharedGraphSyncSession::EnsureNodeStateRequest(
    ae::ObjId requested_node_id) {
  assert(requested_node_id.IsValid());
  if (HasPendingNodeStateRequest(requested_node_id)) {
    return;
  }

  auto packet =
      NodeStateRequestPacket::ptr::Create(ae::CreateWith{codec_.domain()});
  packet->requested_node_id = requested_node_id;
  packet.Save();

  PendingPacket pending;
  pending.packet_id = packet.id();
  pending.bytes = codec_.Encode(packet);
  pending.kind = PendingKind::kNodeStateRequest;
  pending.node_id = requested_node_id;

  send_(pending.bytes);
  pending_packets_.push_back(std::move(pending));
}

void SharedGraphSyncSession::ResendPendingNodeState(ae::ObjId node_id) {
  for (auto const& pending : pending_packets_) {
    if (pending.kind == PendingKind::kNodeState &&
        pending.node_id == node_id) {
      send_(pending.bytes);
      return;
    }
  }
}

void SharedGraphSyncSession::StartInitialSynchronization() {
  assert(!initial_sync_started_);
  initial_sync_started_ = true;

  auto root = LoadLocalNode(local_.shared_root_id);
  auto const discovered = DiscoverSharedGraph(root);
  assert(!discovered.empty());

  for (auto const& node : discovered) {
    node.Load();
    assert(node.is_loaded());
    SendNodeState(node, /*is_initial=*/true);
  }

  MaybeCompleteInitialSync();
}

void SharedGraphSyncSession::Poll() {
  auto root = LoadLocalNode(local_.shared_root_id);
  auto const discovered = DiscoverSharedGraph(root);

  for (auto const& node : discovered) {
    node.Load();
    assert(node.is_loaded());
    if (!ContainsId(known_node_ids_, node.id()) &&
        !HasPendingNodeState(node.id())) {
      SendNodeState(node, /*is_initial=*/false);
    }
  }

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
  for (auto const& pending : pending_packets_) {
    send_(pending.bytes);
  }
}

void SharedGraphSyncSession::Receive(SerializedSyncPacket const& bytes) {
  auto packet = codec_.Decode(bytes);
  assert(packet.is_loaded());
  auto const packet_id = packet.id();

  if (packet->GetClassId() == AckPacket::kClassId) {
    packet->Dispatch(*this);
    return;
  }

  if (ContainsId(successfully_received_packet_ids_, packet_id)) {
    SendAck(packet_id);
    return;
  }

  receiving_packet_id_ = packet_id;
  packet->Dispatch(*this);
  receiving_packet_id_ = {};
}

void SharedGraphSyncSession::Handle(NodeStatePacket const& packet) {
  assert(receiving_packet_id_.IsValid());
  for (auto const& required : packet.required_nodes) {
    if (!StorageHasNode(required)) {
      EnsureNodeStateRequest(required);
      return;
    }
  }

  ApplyNodeState(packet.state, local_);
  AddId(known_node_ids_, packet.state.root_id);
  auto node = LoadLocalNode(packet.state.root_id);
  for (auto const& record : node->journal) {
    AddId(delivered_event_ids_, record.event.id());
  }
  MarkReceivedAndAck(receiving_packet_id_);
}

void SharedGraphSyncSession::Handle(EventPacket const& packet) {
  assert(receiving_packet_id_.IsValid());
  if (!StorageHasNode(packet.target_node_id)) {
    EnsureNodeStateRequest(packet.target_node_id);
    return;
  }

  for (auto const& required : packet.required_nodes) {
    if (!StorageHasNode(required)) {
      EnsureNodeStateRequest(required);
      return;
    }
  }

  ImportObjectState(packet.state, local_.storage);
  auto event = Event::ptr::Declare(
      ae::CreateWith{local_.domain}.with_id(packet.state.root_id));
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
  AddId(delivered_event_ids_, packet.state.root_id);
  MarkReceivedAndAck(receiving_packet_id_);
}

void SharedGraphSyncSession::Handle(NodeStateRequestPacket const& packet) {
  assert(receiving_packet_id_.IsValid());
  assert(StorageHasNode(packet.requested_node_id));

  if (HasPendingNodeState(packet.requested_node_id)) {
    ResendPendingNodeState(packet.requested_node_id);
  } else {
    auto node = LoadLocalNode(packet.requested_node_id);
    SendNodeState(node, /*is_initial=*/false);
  }

  MarkReceivedAndAck(receiving_packet_id_);
}

void SharedGraphSyncSession::Handle(AckPacket const& packet) {
  auto* pending = FindPending(packet.acknowledged_packet_id);
  if (pending == nullptr) {
    return;
  }

  if (pending->kind == PendingKind::kNodeState) {
    AddId(known_node_ids_, pending->node_id);
    for (auto const& event_id : pending->event_ids) {
      AddId(delivered_event_ids_, event_id);
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

  MaybeCompleteInitialSync();
}

}  // namespace apptraverse
