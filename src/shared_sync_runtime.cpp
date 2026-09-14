#include "apptraverse/shared_sync_runtime.h"

#include <algorithm>
#include <cassert>
#include <utility>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/obj_id.h"
#include "aether-objects/obj/registry.h"

#include "apptraverse/event.h"
#include "apptraverse/shared_event_order.h"
#include "apptraverse/shared_network_graph.h"

namespace apptraverse {
namespace {

// Transport address of a Share's Link, or nullptr when the descriptor is not
// in the graph. An imported snapshot is untrusted, so a Share can name a Link
// whose object never arrived.
std::string const* ShareEndpoint(Share const& share) {
  if (!share.link.is_valid()) {
    return nullptr;
  }
  if (!share.link.is_loaded()) {
    share.link.Load();
  }
  if (!share.link.is_loaded()) {
    return nullptr;
  }
  return &share.link->EndpointUid();
}

std::string const* ShareEndpointOf(SharedNode const& node,
                                   ae::ObjId share_id) {
  auto const share_index = node.FindShareIndexForShare(share_id);
  if (share_index >= node.shares.size()) {
    return nullptr;
  }
  return ShareEndpoint(node.shares[share_index]);
}

bool TopologyKnowsEndpoint(SharedNode const& node,
                           std::string const& endpoint_uid) {
  for (auto const& share : node.shares) {
    auto const* endpoint = ShareEndpoint(share);
    if (endpoint != nullptr && *endpoint == endpoint_uid) {
      return true;
    }
  }
  return false;
}

// A NodeState is for this replica only when the relationship it names ends at
// this endpoint and the sender is another endpoint of the same topology.
bool AddressedToThisReplica(SharedNode const& node,
                            std::string const& local_endpoint,
                            std::string const& source_endpoint,
                            ae::ObjId destination_share_id) {
  if (source_endpoint == local_endpoint) {
    return false;
  }
  auto const* destination = ShareEndpointOf(node, destination_share_id);
  if (destination == nullptr || *destination != local_endpoint) {
    return false;
  }
  return TopologyKnowsEndpoint(node, source_endpoint);
}

// Admission rules for an untrusted snapshot, checked in a scratch Domain.
// Everything the import step goes on to dereference is verified here.
bool SnapshotIsAdmissible(SharedNode& candidate,
                          std::string const& local_endpoint,
                          std::string const& source_endpoint,
                          ae::ObjId destination_share_id) {
  if (!candidate.base.is_valid() || !candidate.base.is_loaded()) {
    return false;
  }
  for (auto const& record : candidate.journal) {
    if (!record.event.is_valid() || !record.event.is_loaded()) {
      return false;
    }
    if (ae::Registry::GetRegistry().GenerationDistance(
            record.event->TargetClassId(), candidate.GetClassId()) < 0) {
      return false;
    }
    if (record.order.timestamp_us == 0) {
      return false;
    }
  }
  // Validate the imported journal by replaying it in scratch in timestamp order,
  // from base to final state, ensuring every event was admissible at its replay point.
  if (!candidate.TryReplayFromBase()) {
    return false;
  }
  for (std::size_t i = 0; i < candidate.shares.size(); ++i) {
    auto const& share = candidate.shares[i];
    if (!share.share_id.is_valid()) {
      return false;
    }
    // Relationship identity is what both replicas key their sync state on, so
    // it has to be unambiguous within the snapshot.
    if (candidate.FindShareIndexForShare(share.share_id) != i) {
      return false;
    }
    auto const* endpoint = ShareEndpoint(share);
    if (endpoint == nullptr || endpoint->empty()) {
      return false;
    }
  }
  return AddressedToThisReplica(candidate, local_endpoint, source_endpoint,
                                destination_share_id);
}

Share const* ShareOfEndpoint(SharedNode const& node,
                             std::string const& endpoint_uid) {
  for (auto const& share : node.shares) {
    auto const* endpoint = ShareEndpoint(share);
    if (endpoint != nullptr && *endpoint == endpoint_uid) {
      return &share;
    }
  }
  return nullptr;
}

// Incremental Event: destination relationship ends here, sender is a
// ReadWrite participant of the same topology.
bool EventAddressedToThisReplica(SharedNode const& node,
                                 std::string const& local_endpoint,
                                 std::string const& source_endpoint,
                                 ae::ObjId destination_share_id) {
  if (source_endpoint == local_endpoint) {
    return false;
  }
  auto const* destination = ShareEndpointOf(node, destination_share_id);
  if (destination == nullptr || *destination != local_endpoint) {
    return false;
  }
  auto const* source = ShareOfEndpoint(node, source_endpoint);
  if (source == nullptr) {
    return false;
  }
  return source->GetAccess() == ShareAccess::ReadWrite;
}

// Allocate an ObjId that is guaranteed not to exist in storage or live domain.
ae::ObjId AllocateUniqueStorageObjId(ae::Domain const& domain,
                                     ae::IDomainStorage& storage) {
  while (true) {
    auto const id = ae::ObjId::GenerateUnique();
    if (!id.is_valid()) {
      continue;
    }
    if (domain.Find(id)) {
      continue;
    }
    if (!storage.Enumerate(id).empty()) {
      continue;
    }
    return id;
  }
}

// Preflight the insertion of a candidate event into a scratch copy of the
// target SharedNode to verify historical replay validity before modifying
// production state.
bool PreflightHistoricalEventInsertion(
    SharedNode const& target_node,
    ae::RamDomainStorage const& parsed_event_storage,
    SharedEventId const& identity,
    std::uint64_t timestamp_us) {
  ae::RamDomainStorage scratch_storage;
  BuildNetworkSharedScratch(target_node, scratch_storage);

  ae::Domain scratch_domain{scratch_storage};

  // Transfer the candidate event into the same scratch storage under an unused scratch id
  ae::ObjId const scratch_event_id =
      AllocateUniqueStorageObjId(scratch_domain, scratch_storage);
  CommitStandaloneEventObject(parsed_event_storage, scratch_event_id,
                              scratch_storage);

  ae::DomainGraph scratch_graph{&scratch_domain};

  auto loaded_node = scratch_graph.LoadRoot(target_node.obj_id);
  if (!loaded_node) {
    return false;
  }
  auto loaded_event = scratch_graph.LoadRoot(scratch_event_id);
  if (!loaded_event) {
    return false;
  }
  if (ae::Registry::GetRegistry().GenerationDistance(
          Event::kClassId, loaded_event->GetClassId()) < 0) {
    return false;
  }
  auto& scratch_shared_node = static_cast<SharedNode&>(*loaded_node);
  auto scratch_event_ptr =
      Event::ptr::MakeFromThis(static_cast<Event*>(loaded_event.get()));

  return scratch_shared_node.TryInsertShared(
      std::move(scratch_event_ptr), identity,
      SharedEventOrder{.timestamp_us = timestamp_us});
}

EventRecord const* NextUndeliveredSharedEvent(
    SharedNode const& node, LinkSyncState const& state,
    std::string const& destination_endpoint) {
  for (auto const& record : node.journal) {
    if (!record.HasSharedIdentity()) {
      continue;
    }
    if (state.HasDelivered(record.identity)) {
      continue;
    }
    if (state.HasPendingEvent() &&
        state.pending_event_identity == record.identity) {
      continue;
    }
    // Do not send an Event back to the endpoint it originated from.
    if (record.identity.origin_uid == destination_endpoint) {
      continue;
    }
    return &record;
  }
  return nullptr;
}

bool SameStandalonePayload(Event const& event,
                           std::vector<std::uint8_t> const& payload) {
  std::vector<std::uint8_t> frozen;
  if (!FreezeStandaloneEventPayload(event, frozen)) {
    return false;
  }
  return frozen == payload;
}

}  // namespace

SharedSyncRuntime::SharedSyncRuntime(ae::Domain& domain,
                                     ae::IDomainStorage& storage,
                                     IByteTransport& transport)
    : domain_{domain}, storage_{storage}, transport_{transport} {
  transport_.BindReceive(this, &SharedSyncRuntime::ReceiveThunk);
}

SharedSyncRuntime::~SharedSyncRuntime() { transport_.ClearReceive(); }

void SharedSyncRuntime::RegisterNode(SharedNode::ptr node) {
  assert(node.is_valid());
  assert(node.is_loaded());
  assert(!FindNode(node.id()).is_valid() && "SharedNode registered twice");
  nodes_.push_back(std::move(node));
}

void SharedSyncRuntime::ExpectInitialNode(ae::ObjId node_id) {
  assert(node_id.is_valid());
  expected_initial_nodes_.push_back(node_id);
}

SharedNode::ptr SharedSyncRuntime::FindNode(ae::ObjId node_id) const {
  for (auto const& node : nodes_) {
    if (node.id() == node_id) {
      return node;
    }
  }
  return SharedNode::ptr{};
}

bool SharedSyncRuntime::IsExpectedInitialNode(ae::ObjId node_id) const {
  return std::find(expected_initial_nodes_.begin(),
                   expected_initial_nodes_.end(),
                   node_id) != expected_initial_nodes_.end();
}

void SharedSyncRuntime::SyncInitialState(ae::ObjId node_id,
                                         ae::ObjId share_id) {
  auto node = FindNode(node_id);
  assert(node.is_valid() && "SyncInitialState requires a registered SharedNode");

  auto const* destination_endpoint = ShareEndpointOf(*node, share_id);
  assert(destination_endpoint != nullptr &&
         "SyncInitialState requires an open Share relationship");
  auto const destination = *destination_endpoint;
  assert(!destination.empty() && "Share Link has no transport endpoint");
  assert(destination != transport_.local_endpoint_uid() &&
         "a relationship with this replica's own endpoint is not synchronized");

  auto const sync_index = node->FindLinkSyncIndexForShare(share_id);
  assert(sync_index < node->link_sync_states.size());
  auto state = node->link_sync_states[sync_index];
  if (!state.is_loaded()) {
    state.Load();
  }

  switch (state->GetInitialSyncPhase()) {
    case InitialSyncPhase::Complete:
      return;
    case InitialSyncPhase::Pending:
      // Exact retry: the persisted bytes, never a packet rebuilt from the
      // Node as it looks now.
      transport_.Send(destination, state->pending_initial_packet);
      return;
    case InitialSyncPhase::NotStarted:
      break;
  }

  // Packet identity is the Event identity, so it survives restart and can be
  // embedded in the frame before the Event is committed.
  auto event = BeginInitialSyncEvent::ptr::Create(ae::CreateWith{domain_});
  auto frozen = FreezeNetworkSharedNodeState(*node);
  NodeStateFrame const frame{
      .packet_id = event.id(),
      .target_node_id = node_id,
      .destination_share_id = share_id,
      .payload = std::move(frozen.payload),
  };
  event->packet = EncodeNodeStateFrame(frame);
  event->covered_event_ids = std::move(frozen.covered_event_ids);
  state->Commit(event);

  // Freeze and persist before the first send: bytes that were sent but not
  // persisted could not be retried unchanged after a restart.
  node.Save();
  state.Save();
  transport_.Send(destination, state->pending_initial_packet);
}

void SharedSyncRuntime::SyncNextEvent(ae::ObjId node_id, ae::ObjId share_id) {
  auto node = FindNode(node_id);
  assert(node.is_valid() && "SyncNextEvent requires a registered SharedNode");

  auto const* destination_endpoint = ShareEndpointOf(*node, share_id);
  assert(destination_endpoint != nullptr &&
         "SyncNextEvent requires an open Share relationship");
  auto const destination = *destination_endpoint;
  assert(!destination.empty() && "Share Link has no transport endpoint");
  assert(destination != transport_.local_endpoint_uid() &&
         "a relationship with this replica's own endpoint is not synchronized");

  auto const sync_index = node->FindLinkSyncIndexForShare(share_id);
  assert(sync_index < node->link_sync_states.size());
  auto state = node->link_sync_states[sync_index];
  if (!state.is_loaded()) {
    state.Load();
  }

  if (state->GetInitialSyncPhase() != InitialSyncPhase::Complete) {
    return;
  }
  if (state->HasPendingEvent()) {
    transport_.Send(destination, state->pending_event_packet);
    return;
  }

  auto const* record = NextUndeliveredSharedEvent(*node, *state, destination);
  if (record == nullptr) {
    return;
  }
  assert(record->event.is_valid());
  assert(record->event.is_loaded());

  std::vector<std::uint8_t> payload;
  if (!FreezeStandaloneEventPayload(*record->event, payload)) {
    return;
  }

  auto event =
      BeginIncrementalEventSyncEvent::ptr::Create(ae::CreateWith{domain_});
  EventFrame const frame{
      .packet_id = event.id(),
      .target_node_id = node_id,
      .destination_share_id = share_id,
      .identity = record->identity,
      .timestamp_us = record->order.timestamp_us,
      .event_class_id = record->event->GetClassId(),
      .payload = std::move(payload),
  };
  event->identity = record->identity;
  event->packet = EncodeEventFrame(frame);
  state->Commit(event);

  node.Save();
  state.Save();
  transport_.Send(destination, state->pending_event_packet);
}

void SharedSyncRuntime::ReceiveThunk(void* ctx,
                                     std::string const& source_endpoint,
                                     std::vector<std::uint8_t> const& bytes) {
  static_cast<SharedSyncRuntime*>(ctx)->OnBytes(source_endpoint, bytes);
}

void SharedSyncRuntime::OnBytes(std::string const& source_endpoint,
                                std::vector<std::uint8_t> const& bytes) {
  SyncFrameType type{};
  if (!PeekSyncFrameType(bytes, type)) {
    return;
  }
  switch (type) {
    case SyncFrameType::kNodeState: {
      NodeStateFrame frame;
      if (DecodeNodeStateFrame(bytes, frame)) {
        OnNodeState(source_endpoint, frame);
      }
      break;
    }
    case SyncFrameType::kAck: {
      AckFrame frame;
      if (DecodeAckFrame(bytes, frame)) {
        OnAck(source_endpoint, frame);
      }
      break;
    }
    case SyncFrameType::kEvent: {
      EventFrame frame;
      if (DecodeEventFrame(bytes, frame)) {
        OnEvent(source_endpoint, frame);
      }
      break;
    }
  }
}

SharedNode::ptr SharedSyncRuntime::ImportValidatedNode(
    std::string const& source_endpoint, NodeStateFrame const& frame) {
  // Untrusted bytes may not create arbitrary roots: only a SharedNode this
  // replica is waiting for.
  if (!IsExpectedInitialNode(frame.target_node_id)) {
    return SharedNode::ptr{};
  }

  ae::RamDomainStorage parsed;
  if (!ParseObjectGraphPayload(frame.payload, parsed)) {
    return SharedNode::ptr{};
  }

  // Validate the parsed object graph's stored class chains BEFORE loading any candidate object.
  std::vector<StoredClassChainInfo> chains;
  if (!ValidateStoredClassChains(parsed, &chains)) {
    return SharedNode::ptr{};
  }

  // Ensure root target object is in the payload and its most-derived class derives SharedNode.
  bool root_found = false;
  for (auto const& chain_info : chains) {
    if (chain_info.obj_id == frame.target_node_id) {
      root_found = true;
      if (ae::Registry::GetRegistry().GenerationDistance(
              SharedNode::kClassId, chain_info.most_derived_class_id) < 0) {
        return SharedNode::ptr{};
      }
      break;
    }
  }
  if (!root_found) {
    return SharedNode::ptr{};
  }

  {
    // Scratch Domain over the parsed bytes: the candidate is inspected here
    // and discarded with it. No production object is created, and this
    // replica's own storage stays untouched until the snapshot is admitted.
    ae::Domain scratch_domain{parsed};
    ae::DomainGraph scratch_graph{&scratch_domain};
    auto candidate = scratch_graph.LoadRoot(frame.target_node_id);
    if (!candidate) {
      return SharedNode::ptr{};
    }
    if (ae::Registry::GetRegistry().GenerationDistance(
            SharedNode::kClassId, candidate->GetClassId()) < 0) {
      return SharedNode::ptr{};
    }
    if (!SnapshotIsAdmissible(static_cast<SharedNode&>(*candidate),
                              transport_.local_endpoint_uid(), source_endpoint,
                              frame.destination_share_id)) {
      return SharedNode::ptr{};
    }
  }

  CommitObjectGraph(parsed, storage_);
  auto node = SharedNode::ptr::Declare(
      ae::CreateWith{domain_}.with_id(frame.target_node_id));
  node.Load();
  assert(node.is_loaded() && "admitted snapshot must load from own storage");
  // Sender-local sync state is excluded from the snapshot by design.
  // Replaying the imported shared journal builds this replica's own
  // LinkSyncState for every Share, keyed by the relationship identity that
  // travelled with the topology.
  node->ReplayFromBase();
  return node;
}

void SharedSyncRuntime::OnNodeState(std::string const& source_endpoint,
                                    NodeStateFrame const& frame) {
  auto node = FindNode(frame.target_node_id);
  bool const imported = !node.is_valid();
  if (imported) {
    node = ImportValidatedNode(source_endpoint, frame);
    if (!node.is_valid()) {
      return;
    }
  } else if (!AddressedToThisReplica(*node, transport_.local_endpoint_uid(),
                                     source_endpoint,
                                     frame.destination_share_id)) {
    return;
  }

  auto const sync_index =
      node->FindLinkSyncIndexForShare(frame.destination_share_id);
  assert(sync_index < node->link_sync_states.size() &&
         "every Share of a live SharedNode has local sync state");
  auto state = node->link_sync_states[sync_index];
  if (!state.is_loaded()) {
    state.Load();
  }

  if (state->received_initial_packet_id != frame.packet_id) {
    if (state->received_initial_packet_id.is_valid()) {
      // A second, different initial snapshot is not part of protocol v1.
      return;
    }
    state->NoteInitialSyncReceived(frame.packet_id);
    node.Save();
    state.Save();
  }

  // Registered only once the snapshot is validated, imported, and durable.
  if (imported) {
    nodes_.push_back(node);
  }

  // Persisted now, or already persisted when this packet first arrived: a
  // duplicate is acknowledged again and applied once.
  transport_.Send(source_endpoint,
                  EncodeAckFrame(AckFrame{
                      .packet_id = frame.packet_id,
                      .target_node_id = frame.target_node_id,
                      .destination_share_id = frame.destination_share_id,
                  }));
}

void SharedSyncRuntime::OnAck(std::string const& source_endpoint,
                              AckFrame const& frame) {
  auto node = FindNode(frame.target_node_id);
  if (!node.is_valid()) {
    return;
  }
  // Only the endpoint the relationship points at can acknowledge it. The
  // packet fields alone say nothing about who actually sent these bytes.
  auto const* destination =
      ShareEndpointOf(*node, frame.destination_share_id);
  if (destination == nullptr || *destination != source_endpoint) {
    return;
  }
  auto const sync_index =
      node->FindLinkSyncIndexForShare(frame.destination_share_id);
  if (sync_index >= node->link_sync_states.size()) {
    return;
  }
  auto state = node->link_sync_states[sync_index];
  if (!state.is_loaded()) {
    state.Load();
  }
  if (state->GetInitialSyncPhase() == InitialSyncPhase::Pending &&
      state->pending_initial_packet_id == frame.packet_id) {
    state->CompleteInitialSync();
    node.Save();
    state.Save();
    return;
  }
  if (state->GetInitialSyncPhase() == InitialSyncPhase::Complete &&
      state->pending_event_packet_id == frame.packet_id) {
    state->CompleteIncrementalEvent();
    node.Save();
    state.Save();
  }
}

void SharedSyncRuntime::OnEvent(std::string const& source_endpoint,
                                EventFrame const& frame) {
  auto node = FindNode(frame.target_node_id);
  if (!node.is_valid()) {
    return;
  }
  if (!EventAddressedToThisReplica(*node, transport_.local_endpoint_uid(),
                                   source_endpoint,
                                   frame.destination_share_id)) {
    return;
  }

  ae::RamDomainStorage parsed;
  if (!ParseStandaloneEventPayload(frame.payload, parsed)) {
    return;
  }

  // Validate stored class chains before any LoadRoot on scratch storage
  if (!ValidateStandaloneEventStorage(parsed, frame.event_class_id)) {
    return;
  }

  {
    ae::Domain scratch_domain{parsed};
    ae::DomainGraph scratch_graph{&scratch_domain};
    auto candidate = scratch_graph.LoadRoot(kStandaloneEventScratchId);
    if (!candidate) {
      return;
    }
    if (ae::Registry::GetRegistry().GenerationDistance(
            Event::kClassId, candidate->GetClassId()) < 0) {
      return;
    }
    if (candidate->GetClassId() != frame.event_class_id) {
      return;
    }
    auto& scratch_event = static_cast<Event&>(*candidate);
    if (ae::Registry::GetRegistry().GenerationDistance(
            scratch_event.TargetClassId(), node->GetClassId()) < 0) {
      return;
    }
  }

  if (auto const* existing = node->FindSharedEvent(frame.identity)) {
    if (existing->order.timestamp_us != frame.timestamp_us ||
        !existing->event.is_valid() || !existing->event.is_loaded() ||
        existing->event->GetClassId() != frame.event_class_id ||
        !SameStandalonePayload(*existing->event, frame.payload)) {
      return;
    }
    transport_.Send(source_endpoint,
                    EncodeAckFrame(AckFrame{
                        .packet_id = frame.packet_id,
                        .target_node_id = frame.target_node_id,
                        .destination_share_id = frame.destination_share_id,
                    }));
    return;
  }

  // Preflight replay in a non-production scratch copy of the target SharedNode
  // at its historical insertion point.
  if (!PreflightHistoricalEventInsertion(*node, parsed, frame.identity,
                                         frame.timestamp_us)) {
    return;
  }

  ae::ObjId const local_id = AllocateUniqueStorageObjId(domain_, storage_);
  CommitStandaloneEventObject(parsed, local_id, storage_);
  auto local_event =
      Event::ptr::Declare(ae::CreateWith{domain_}.with_id(local_id));
  local_event.Load();
  assert(local_event.is_loaded() &&
         "admitted standalone Event must load from own storage");
  assert(local_event->GetClassId() == frame.event_class_id);

  node->InsertShared(
      std::move(local_event), frame.identity,
      SharedEventOrder{.timestamp_us = frame.timestamp_us});
  node.Save();

  transport_.Send(source_endpoint,
                  EncodeAckFrame(AckFrame{
                      .packet_id = frame.packet_id,
                      .target_node_id = frame.target_node_id,
                      .destination_share_id = frame.destination_share_id,
                  }));
}

}  // namespace apptraverse
