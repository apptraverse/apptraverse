#include "apptraverse/shared_graph_sync_session.h"

#include <algorithm>
#include <cassert>
#include <string>
#include <utility>

#include "aether/clock.h"

#include "apptraverse/node.h"
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
                                               SyncSessionState::ptr state,
                                               SendFunction send)
    : local_{local_replica}, state_{std::move(state)}, send_{std::move(send)} {
  assert(send_);
  assert(state_.is_valid());
  assert(state_.is_loaded());
  assert(local_.shared_root_id.IsValid());
  assert(state_->data.shared_root_id == local_.shared_root_id);
}

void SharedGraphSyncSession::Trace(std::string const& line) const {
  if (trace_) {
    trace_(line);
  }
}

void SharedGraphSyncSession::CommitData(SyncSessionData data) {
  auto event = SetSyncSessionDataEvent::ptr::Create(
      ae::CreateWith{*state_.domain()});
  event->data = std::move(data);
  state_->Commit(std::move(event));
  state_.Save();
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
  for (auto const& pending : state_->data.pending_packets) {
    if (pending.kind == PendingSyncPacketKind::kEvent &&
        pending.event_id == event_id) {
      return true;
    }
  }
  return false;
}

bool SharedGraphSyncSession::HasPendingInitialNodeState() const {
  for (auto const& pending : state_->data.pending_packets) {
    if (pending.kind == PendingSyncPacketKind::kNodeState &&
        pending.is_initial_state) {
      return true;
    }
  }
  return false;
}

bool SharedGraphSyncSession::IsEventCoveredByPendingNodeState(
    ae::ObjId event_id) const {
  for (auto const& pending : state_->data.pending_packets) {
    if (pending.kind != PendingSyncPacketKind::kNodeState) {
      continue;
    }
    if (ContainsId(pending.event_ids, event_id)) {
      return true;
    }
  }
  return false;
}

PendingSyncPacketState* SharedGraphSyncSession::FindPending(
    SyncSessionData& data, ae::ObjId packet_id) {
  for (auto& pending : data.pending_packets) {
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
  auto bytes = SyncPacketCodec{}.Encode(ack);
  Trace("SYNC_ACK_SENT ack_packet=" + std::to_string(ack.id().id()) +
        " acknowledged=" + std::to_string(acknowledged_packet_id.id()) +
        " t_us=" + std::to_string(SystemUtcMicros()));
  send_(ack.id(), std::move(bytes));
}

void SharedGraphSyncSession::MarkReceivedAndAck(ae::ObjId packet_id) {
  auto data = state_->data;
  AddId(data.successfully_received_packet_ids, packet_id);
  CommitData(std::move(data));
  SendAck(packet_id);
}

void SharedGraphSyncSession::SendInitialNodeState(Node::ptr root) {
  assert(root.is_valid());
  assert(root.is_loaded());
  assert(!HasPendingInitialNodeState());

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

  PendingSyncPacketState pending;
  pending.packet_id = packet.id();
  pending.serialized_bytes = SyncPacketCodec{}.Encode(packet);
  pending.kind = PendingSyncPacketKind::kNodeState;
  pending.node_id = root.id();
  pending.event_ids = CollectSharedGraphEventIds(root);
  pending.is_initial_state = true;

  auto bytes = pending.serialized_bytes;
  auto const packet_id = pending.packet_id;
  auto data = state_->data;
  data.pending_packets.push_back(std::move(pending));
  CommitData(std::move(data));
  Trace("SYNC_PACKET_CREATED kind=node_state packet=" +
        std::to_string(packet_id.id()) + " event=0 target=" +
        std::to_string(root.id().id()) +
        " t_us=" + std::to_string(SystemUtcMicros()));
  send_(packet_id, std::move(bytes));
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

  PendingSyncPacketState pending;
  pending.packet_id = packet.id();
  pending.serialized_bytes = SyncPacketCodec{}.Encode(packet);
  pending.kind = PendingSyncPacketKind::kEvent;
  pending.node_id = node.id();
  pending.event_id = event.id();

  auto bytes = pending.serialized_bytes;
  auto const packet_id = pending.packet_id;
  auto data = state_->data;
  data.pending_packets.push_back(std::move(pending));
  CommitData(std::move(data));
  Trace("SYNC_PACKET_CREATED kind=event packet=" +
        std::to_string(packet_id.id()) +
        " event=" + std::to_string(event.id().id()) +
        " target=" + std::to_string(node.id().id()) +
        " t_us=" + std::to_string(SystemUtcMicros()));
  send_(packet_id, std::move(bytes));
}

void SharedGraphSyncSession::StartOrResume() {
  if (!state_->data.initial_sync_started) {
    auto data = state_->data;
    data.initial_sync_started = true;
    CommitData(std::move(data));
  }

  if (!state_->data.initial_sync_complete) {
    struct PendingCopy {
      ae::ObjId packet_id;
      SerializedSyncPacket bytes;
    };
    std::vector<PendingCopy> prior_pending;
    for (auto const& pending : state_->data.pending_packets) {
      prior_pending.push_back(
          PendingCopy{pending.packet_id, pending.serialized_bytes});
    }

    if (!HasPendingInitialNodeState()) {
      auto root = LoadLocalNode(local_.shared_root_id);
      SendInitialNodeState(root);
    }

    for (auto& item : prior_pending) {
      Trace("SYNC_PACKET_RETRY packet=" + std::to_string(item.packet_id.id()) +
            " t_us=" + std::to_string(SystemUtcMicros()));
      send_(item.packet_id, std::move(item.bytes));
    }
    return;
  }

  RetryPending();
  Poll();
}

void SharedGraphSyncSession::Poll() {
  if (!state_->data.initial_sync_complete) {
    return;
  }

  auto root = LoadLocalNode(local_.shared_root_id);
  auto const discovered = DiscoverSharedGraph(root);
  for (auto const& node : discovered) {
    node.Load();
    assert(node.is_loaded());
    for (auto const& record : node->journal) {
      auto const event_id = record.event.id();
      if (ContainsId(state_->data.delivered_event_ids, event_id)) {
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
  struct PendingCopy {
    ae::ObjId packet_id;
    SerializedSyncPacket bytes;
  };
  std::vector<PendingCopy> copies;
  copies.reserve(state_->data.pending_packets.size());
  for (auto const& pending : state_->data.pending_packets) {
    copies.push_back(PendingCopy{pending.packet_id, pending.serialized_bytes});
  }
  for (auto& item : copies) {
    Trace("SYNC_PACKET_RETRY packet=" + std::to_string(item.packet_id.id()) +
          " t_us=" + std::to_string(SystemUtcMicros()));
    send_(item.packet_id, std::move(item.bytes));
  }
}

void SharedGraphSyncSession::Receive(SerializedSyncPacket const& bytes) {
  auto decoded = SyncPacketCodec{}.Decode(bytes);
  assert(decoded.packet.is_loaded());
  auto const packet_id = decoded.packet.id();
  auto const class_id = decoded.packet->GetClassId();
  char const* kind = "unknown";
  if (class_id == AckPacket::kClassId) {
    kind = "ack";
  } else if (class_id == NodeStatePacket::kClassId) {
    kind = "node_state";
  } else if (class_id == EventPacket::kClassId) {
    kind = "event";
  }
  Trace("SYNC_PACKET_RECEIVED kind=" + std::string{kind} +
        " packet=" + std::to_string(packet_id.id()) +
        " t_us=" + std::to_string(SystemUtcMicros()));

  if (class_id == AckPacket::kClassId) {
    receiving_decoded_ = &decoded;
    decoded.packet->Dispatch(*this);
    receiving_decoded_ = nullptr;
    return;
  }

  if (ContainsId(state_->data.successfully_received_packet_ids, packet_id)) {
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
  for (auto const& decoded_node : decoded_nodes) {
    decoded_node.Load();
    assert(decoded_node.is_loaded());
    if (!StorageHasNode(decoded_node.id())) {
      ImportObjectGraph(decoded_node, decoded_storage, local_,
                        SharedCopyMode::kCopyLoadedTargets);
    }
  }

  auto entries = CollectOrderedJournalEntries(decoded_nodes);
  auto data = state_->data;
  for (auto const& entry : entries) {
    auto local_node = LoadLocalNode(entry.node.id());
    if (local_node->HasEvent(entry.record.event.id())) {
      AddId(data.delivered_event_ids, entry.record.event.id());
      continue;
    }
    auto decoded_event = entry.record.event;
    decoded_event.Load();
    assert(decoded_event.is_loaded());
    auto imported = ImportEventForAccept(decoded_event, decoded_storage);
    auto const result = local_node->TryAcceptRemoteEvent(
        std::move(imported), entry.record.timestamp_us);
    assert(result != RemoteEventResult::kBlocked);
    if (result == RemoteEventResult::kAccepted) {
      local_node.Save();
    }
    AddId(data.delivered_event_ids, entry.record.event.id());
  }
  CommitData(std::move(data));
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
    return;
  }

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
    Trace("SYNC_EVENT_BLOCKED packet=" +
          std::to_string(receiving_packet_id_.id()) +
          " event=" + std::to_string(packet.event.id().id()) +
          " target=" + std::to_string(packet.target_node_id.id()) +
          " t_us=" + std::to_string(SystemUtcMicros()));
    return;
  }

  if (result == RemoteEventResult::kAccepted) {
    target.Save();
  }

  auto data = state_->data;
  AddId(data.delivered_event_ids, packet.event.id());
  CommitData(std::move(data));
  Trace("SYNC_EVENT_APPLIED packet=" +
        std::to_string(receiving_packet_id_.id()) +
        " event=" + std::to_string(packet.event.id().id()) +
        " target=" + std::to_string(packet.target_node_id.id()) +
        " t_us=" + std::to_string(SystemUtcMicros()));
  MarkReceivedAndAck(receiving_packet_id_);
}

void SharedGraphSyncSession::Handle(AckPacket const& packet) {
  auto data = state_->data;
  auto* pending = FindPending(data, packet.acknowledged_packet_id);
  if (pending == nullptr) {
    return;
  }

  Trace("SYNC_ACK_RECEIVED ack_packet=" +
        std::to_string(receiving_decoded_ != nullptr
                           ? receiving_decoded_->packet.id().id()
                           : 0) +
        " acknowledged=" +
        std::to_string(packet.acknowledged_packet_id.id()) +
        " t_us=" + std::to_string(SystemUtcMicros()));

  if (pending->kind == PendingSyncPacketKind::kNodeState) {
    for (auto const& event_id : pending->event_ids) {
      AddId(data.delivered_event_ids, event_id);
    }
    if (pending->is_initial_state) {
      data.initial_sync_complete = true;
    }
  } else if (pending->kind == PendingSyncPacketKind::kEvent) {
    AddId(data.delivered_event_ids, pending->event_id);
  }

  auto const packet_id = pending->packet_id;
  data.pending_packets.erase(
      std::remove_if(data.pending_packets.begin(), data.pending_packets.end(),
                     [&](PendingSyncPacketState const& item) {
                       return item.packet_id == packet_id;
                     }),
      data.pending_packets.end());
  auto const pending_left = data.pending_packets.size();
  CommitData(std::move(data));
  ++ack_progress_revision_;
  Trace("SYNC_PENDING_REMOVED packet=" + std::to_string(packet_id.id()) +
        " pending=" + std::to_string(pending_left) +
        " t_us=" + std::to_string(SystemUtcMicros()));
}

}  // namespace apptraverse
