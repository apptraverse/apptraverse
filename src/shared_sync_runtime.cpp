#include "apptraverse/shared_sync_runtime.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <set>
#include <utility>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/obj_id.h"
#include "aether-objects/obj/registry.h"

#include "apptraverse/event.h"
#include "apptraverse/runtime_node.h"
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
  // Permanent pair: exactly two ReadWrite shares (self + peer).
  if (candidate.shares.size() != 2) {
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
  if (candidate.shares.size() != 2) {
    return false;
  }
  for (std::size_t i = 0; i < candidate.shares.size(); ++i) {
    auto const& share = candidate.shares[i];
    if (!share.share_id.is_valid()) {
      return false;
    }
    if (share.GetAccess() != ShareAccess::ReadWrite) {
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

// Preflight the insertion of a candidate event into a scratch copy of the
// target SharedNode to verify historical replay validity before modifying
// production state.
bool PreflightHistoricalEventInsertion(
    SharedNode const& target_node,
    ae::RamDomainStorage const& parsed_event_storage,
    ae::ObjId wire_root_id,
    std::uint32_t expected_event_class_id,
    SharedEventId const& identity,
    std::uint64_t timestamp_us) {
  ae::RamDomainStorage scratch_storage;
  BuildNetworkSharedScratch(target_node, scratch_storage);

  ae::Domain scratch_domain{scratch_storage};

  auto candidate = ImportStandaloneEventGraph(
      parsed_event_storage, wire_root_id, expected_event_class_id,
      scratch_domain, scratch_storage);
  if (!candidate) {
    return false;
  }

  ae::DomainGraph scratch_graph{&scratch_domain};
  auto loaded_node = scratch_graph.LoadRoot(target_node.obj_id);
  if (!loaded_node) {
    return false;
  }
  auto& scratch_shared_node = static_cast<SharedNode&>(*loaded_node);

  auto candidate_ptr = Event::ptr::MakeFromThis(candidate.get());
  return scratch_shared_node.TryInsertShared(
      std::move(candidate_ptr), identity,
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

bool SameEventPayload(Event const& event,
                      std::vector<std::uint8_t> const& payload) {
  ae::RamDomainStorage parsed;
  ae::ObjId wire_root_id;
  if (!ParseEventPayload(payload, parsed, wire_root_id)) {
    return false;
  }
  ae::RamDomainStorage event_scratch;
  BuildNetworkSharedScratch(event, event_scratch);
  auto const it_event = event_scratch.state.find(event.obj_id);
  auto const it_wire = parsed.state.find(wire_root_id);
  if (it_event == event_scratch.state.end() || !it_event->second.has_value() ||
      it_wire == parsed.state.end() || !it_wire->second.has_value()) {
    return false;
  }
  return it_event->second == it_wire->second;
}

bool ObjectOccupied(ae::Domain& domain, ae::IDomainStorage& storage,
                    ae::ObjId id) {
  if (domain.Find(id)) {
    return true;
  }
  return !storage.Enumerate(id).empty();
}

std::set<ae::ObjId> SerializedClosure(ae::Obj const& root) {
  ae::RamDomainStorage scratch;
  BuildNetworkSharedScratch(root, scratch);
  std::set<ae::ObjId> ids;
  for (auto const& [obj_id, classes] : scratch.state) {
    if (classes.has_value()) {
      ids.insert(obj_id);
    }
  }
  return ids;
}

StoredClassChainInfo const* ChainOf(
    std::vector<StoredClassChainInfo> const& chains, ae::ObjId id) {
  for (auto const& chain : chains) {
    if (chain.obj_id == id) {
      return &chain;
    }
  }
  return nullptr;
}

ae::Ptr<ae::Obj> FindOrLoad(ae::Domain& domain, ae::IDomainStorage& storage,
                            ae::ObjId id) {
  if (auto found = domain.Find(id)) {
    return found;
  }
  if (storage.Enumerate(id).empty()) {
    return {};
  }
  ae::DomainGraph graph{&domain};
  return graph.LoadRoot(id);
}

// Stored class layers and their version keys. Node journal bytes are not
// compared: a reused Link keeps the receiver's journal.
bool StoredVersionsMatch(ae::IDomainStorage& storage, ae::ObjId id,
                         ae::RamDomainStorage::ClassData const& incoming) {
  auto const listed = storage.Enumerate(id);
  if (listed.size() != incoming.size()) {
    return false;
  }
  for (auto const class_id : listed) {
    if (incoming.find(class_id) == incoming.end()) {
      return false;
    }
  }
  for (auto const& [class_id, versions] : incoming) {
    if (versions.empty()) {
      return false;
    }
    std::uint8_t max_version = 0;
    for (auto const& [version, data] : versions) {
      (void)data;
      if (version > max_version) {
        max_version = version;
      }
    }
    // Current object versions are small. A higher key is not this protocol.
    if (max_version >= 31) {
      return false;
    }
    auto const limit = static_cast<std::uint8_t>(max_version + 1);
    for (std::uint8_t version = 0; version <= limit; ++version) {
      ae::DomainQuery const query{id, class_id, version};
      bool const has =
          storage.Load(query).result == ae::DomainLoadResult::kLoaded;
      bool const want = versions.find(version) != versions.end();
      if (has != want) {
        return false;
      }
    }
  }
  return true;
}

bool OccupiedClassMatches(ae::Domain& domain, ae::IDomainStorage& storage,
                          ae::RamDomainStorage const& parsed,
                          std::vector<StoredClassChainInfo> const& chains,
                          ae::ObjId id) {
  auto const* chain = ChainOf(chains, id);
  if (chain == nullptr) {
    return false;
  }
  auto live = FindOrLoad(domain, storage, id);
  if (!live) {
    return false;
  }
  if (live->GetClassId() != chain->most_derived_class_id) {
    return false;
  }
  auto const it = parsed.state.find(id);
  if (it == parsed.state.end() || !it->second.has_value()) {
    return false;
  }
  return StoredVersionsMatch(storage, id, *it->second);
}

// Identity is the ObjId. Type is the class. Version is the stored class-layer
// keys. The saved descriptor is EndpointUid plus MemoryLink fields, and the
// same checks on base. An endpoint string alone is not enough.
bool LinkDescriptorsMatch(Node& live, Node& incoming) {
  if (live.obj_id != incoming.obj_id ||
      live.GetClassId() != incoming.GetClassId()) {
    return false;
  }
  auto& registry = ae::Registry::GetRegistry();
  if (registry.GenerationDistance(Link::kClassId, live.GetClassId()) < 0) {
    return false;
  }
  auto& live_link = static_cast<Link&>(live);
  auto& incoming_link = static_cast<Link&>(incoming);
  if (live_link.EndpointUid() != incoming_link.EndpointUid()) {
    return false;
  }
  if (live.GetClassId() == MemoryLink::kClassId) {
    auto& live_memory = static_cast<MemoryLink&>(live);
    auto& incoming_memory = static_cast<MemoryLink&>(incoming);
    if (live_memory.endpoint_uid != incoming_memory.endpoint_uid ||
        live_memory.heartbeat_interval_ms !=
            incoming_memory.heartbeat_interval_ms) {
      return false;
    }
  } else if (live.GetClassId() != Link::kClassId) {
    return false;
  }

  bool const live_has_base = live.base.is_valid();
  bool const incoming_has_base = incoming.base.is_valid();
  if (live_has_base != incoming_has_base) {
    return false;
  }
  if (!live_has_base) {
    return true;
  }
  if (live.base.id() != incoming.base.id() || live.base.id() == live.obj_id) {
    return false;
  }
  if (!live.base.is_loaded()) {
    live.base.Load();
  }
  if (!incoming.base.is_loaded()) {
    incoming.base.Load();
  }
  if (!live.base.is_loaded() || !incoming.base.is_loaded()) {
    return false;
  }
  return LinkDescriptorsMatch(*live.base, *incoming.base);
}

// Occupied Link closures that are safe to keep. Returns false when the
// snapshot must be rejected with the receiver graph still untouched.
bool PlanReusableLinks(ae::Domain& domain, ae::IDomainStorage& storage,
                       SharedNode& candidate, ae::RamDomainStorage const& parsed,
                       std::vector<StoredClassChainInfo> const& chains,
                       std::set<ae::ObjId>& skip) {
  std::set<ae::ObjId> seen_links;
  for (auto const& share : candidate.shares) {
    if (!share.link.is_valid()) {
      return false;
    }
    if (!share.link.is_loaded()) {
      share.link.Load();
    }
    if (!share.link.is_loaded()) {
      return false;
    }
    if (!seen_links.insert(share.link.id()).second) {
      continue;
    }
    if (!ObjectOccupied(domain, storage, share.link.id())) {
      continue;
    }

    auto live = FindOrLoad(domain, storage, share.link.id());
    if (!live) {
      return false;
    }
    auto& registry = ae::Registry::GetRegistry();
    if (registry.GenerationDistance(Node::kClassId, live->GetClassId()) < 0 ||
        registry.GenerationDistance(Node::kClassId, share.link->GetClassId()) <
            0) {
      return false;
    }
    auto& live_node = static_cast<Node&>(*live);
    auto& incoming_node = static_cast<Node&>(*share.link);
    if (!LinkDescriptorsMatch(live_node, incoming_node)) {
      return false;
    }
    if (!OccupiedClassMatches(domain, storage, parsed, chains,
                              share.link.id())) {
      return false;
    }
    for (Node* cursor = &live_node; cursor->base.is_valid();) {
      if (!OccupiedClassMatches(domain, storage, parsed, chains,
                                cursor->base.id())) {
        return false;
      }
      if (!cursor->base.is_loaded()) {
        cursor->base.Load();
      }
      cursor = &*cursor->base;
    }

    auto const incoming_closure = SerializedClosure(*share.link);
    auto const existing_closure = SerializedClosure(live_node);
    if (incoming_closure.find(share.link.id()) == incoming_closure.end()) {
      return false;
    }
    for (auto const id : incoming_closure) {
      if (!ObjectOccupied(domain, storage, id)) {
        // A journal object the receiver does not have. Leave the local
        // journal as it is.
        skip.insert(id);
        continue;
      }
      if (id != share.link.id() &&
          existing_closure.find(id) == existing_closure.end()) {
        return false;
      }
      if (id != share.link.id() &&
          !OccupiedClassMatches(domain, storage, parsed, chains, id)) {
        return false;
      }
      skip.insert(id);
    }
  }
  return true;
}

// InstallLocalShare commits AddShare without a shared identity. Those shares
// drive SyncInitialState. Shared-topology AddShare (CatchUp) is gone.
bool DrivesInitialSnapshot(SharedNode const& node, ae::ObjId share_id) {
  for (auto const& record : node.journal) {
    if (!record.event.is_valid()) {
      continue;
    }
    auto event = record.event;
    if (!event.is_loaded()) {
      event.Load();
    }
    if (event->GetClassId() != AddShareEvent::kClassId) {
      continue;
    }
    auto const& add = static_cast<AddShareEvent const&>(*event);
    if (add.share_id != share_id) {
      continue;
    }
    return !record.HasSharedIdentity();
  }
  return false;
}

}  // namespace

SharedSyncRuntime::SharedSyncRuntime(ae::Domain& domain,
                                     ae::IDomainStorage& storage,
                                     IByteTransport& transport)
    : domain_{domain}, storage_{storage}, transport_{transport} {
  transport_.BindReceive(this, &SharedSyncRuntime::ReceiveThunk);
  transport_.BindAvailability(this, &SharedSyncRuntime::AvailabilityThunk);
}

SharedSyncRuntime::~SharedSyncRuntime() {
  transport_.ClearReceive();
  transport_.ClearAvailability();
}

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

void SharedSyncRuntime::ExpectInitialNodeFromEndpoint(
    std::string source_endpoint, std::uint32_t expected_root_class_id,
    ae::ObjId expected_node_id) {
  if (source_endpoint.empty() || expected_root_class_id == 0) {
    return;
  }
  // Same endpoint and same node id (including the empty wildcard) is one
  // slot. A different node id is a different slot and must not replace this.
  for (auto& exp : expected_endpoint_nodes_) {
    if (exp.source_endpoint == source_endpoint &&
        exp.expected_node_id == expected_node_id) {
      exp.expected_root_class_id = expected_root_class_id;
      return;
    }
  }
  expected_endpoint_nodes_.push_back(EndpointExpectation{
      .source_endpoint = std::move(source_endpoint),
      .expected_root_class_id = expected_root_class_id,
      .expected_node_id = expected_node_id,
  });
}

void SharedSyncRuntime::ForgetInitialNodeFromEndpoint(
    std::string const& source_endpoint) {
  if (source_endpoint.empty()) {
    return;
  }
  expected_endpoint_nodes_.erase(
      std::remove_if(expected_endpoint_nodes_.begin(),
                     expected_endpoint_nodes_.end(),
                     [&](EndpointExpectation const& exp) {
                       return exp.source_endpoint == source_endpoint;
                     }),
      expected_endpoint_nodes_.end());
}

void SharedSyncRuntime::ForgetInitialNodeExpectation(
    std::string const& source_endpoint, ae::ObjId node_id) {
  if (source_endpoint.empty()) {
    return;
  }
  expected_endpoint_nodes_.erase(
      std::remove_if(expected_endpoint_nodes_.begin(),
                     expected_endpoint_nodes_.end(),
                     [&](EndpointExpectation const& exp) {
                       return exp.source_endpoint == source_endpoint &&
                              exp.expected_node_id == node_id;
                     }),
      expected_endpoint_nodes_.end());
}

void SharedSyncRuntime::SetInitialNodeImportedCallback(
    InitialNodeImportedCallback callback) {
  initial_node_imported_callback_ = std::move(callback);
}

void SharedSyncRuntime::AllowStandaloneEventClass(std::uint32_t class_id) {
  if (class_id == 0) {
    return;
  }
  if (ae::Registry::GetRegistry().GenerationDistance(Event::kClassId,
                                                     class_id) < 0) {
    return;
  }
  if (!IsStandaloneEventClassAllowed(class_id)) {
    standalone_event_classes_.push_back(class_id);
  }
}

bool SharedSyncRuntime::IsStandaloneEventClassAllowed(
    std::uint32_t class_id) const {
  return std::find(standalone_event_classes_.begin(),
                   standalone_event_classes_.end(),
                   class_id) != standalone_event_classes_.end();
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
      TrySend(destination, state->pending_initial_packet);
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
  TrySend(destination, state->pending_initial_packet);
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
    if (!LocalShareAllowsWrite(*node)) {
      return;
    }
    TrySend(destination, state->pending_event_packet);
    return;
  }

  auto const* record = NextUndeliveredSharedEvent(*node, *state, destination);
  if (record == nullptr) {
    return;
  }
  assert(record->event.is_valid());
  assert(record->event.is_loaded());

  if (!IsStandaloneEventClassAllowed(record->event->GetClassId())) {
    return;
  }
  if (!LocalShareAllowsWrite(*node)) {
    return;
  }

  std::vector<std::uint8_t> payload;
  if (!FreezeEventPayload(*record->event, payload)) {
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
  TrySend(destination, state->pending_event_packet);
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

SharedSyncRuntime::ImportedNode SharedSyncRuntime::ImportValidatedNode(
    std::string const& source_endpoint, NodeStateFrame const& frame) {
  // Untrusted bytes may not create arbitrary roots. Permission is an exact
  // node id this replica is waiting for, or one matching endpoint expectation.
  bool const exact_expected = IsExpectedInitialNode(frame.target_node_id);
  auto const expectation_index =
      MatchEndpointExpectation(source_endpoint, frame.target_node_id);
  bool const endpoint_expected =
      expectation_index < expected_endpoint_nodes_.size();
  std::uint32_t endpoint_expected_class = 0;
  if (endpoint_expected) {
    endpoint_expected_class =
        expected_endpoint_nodes_[expectation_index].expected_root_class_id;
  }

  if (!exact_expected && !endpoint_expected) {
    return {};
  }

  ae::RamDomainStorage parsed;
  if (!DeserializeObjectGraph(frame.payload, parsed)) {
    return {};
  }

  // Validate the parsed object graph's stored class chains BEFORE loading any
  // candidate object.
  std::vector<StoredClassChainInfo> chains;
  if (!ValidateStoredClassChains(parsed, &chains)) {
    return {};
  }

  // Ensure root target object is in the payload and its most-derived class
  // derives SharedNode.
  bool root_found = false;
  for (auto const& chain_info : chains) {
    if (chain_info.obj_id == frame.target_node_id) {
      root_found = true;
      if (ae::Registry::GetRegistry().GenerationDistance(
              SharedNode::kClassId, chain_info.most_derived_class_id) < 0) {
        return {};
      }
      if (endpoint_expected) {
        if (chain_info.most_derived_class_id != endpoint_expected_class) {
          return {};
        }
      }
      break;
    }
  }
  if (!root_found) {
    return {};
  }

  std::set<ae::ObjId> reused_link_objects;
  ae::ObjId matching_share_id;
  {
    // Scratch Domain over the parsed bytes: the candidate is inspected here
    // and discarded with it. No production object is created, and this
    // replica's own storage stays untouched until the snapshot is admitted.
    ae::Domain scratch_domain{parsed};
    ae::DomainGraph scratch_graph{&scratch_domain};
    auto candidate = scratch_graph.LoadRoot(frame.target_node_id);
    if (!candidate) {
      return {};
    }
    if (ae::Registry::GetRegistry().GenerationDistance(
            SharedNode::kClassId, candidate->GetClassId()) < 0) {
      return {};
    }
    auto& shared_candidate = static_cast<SharedNode&>(*candidate);
    if (!SnapshotIsAdmissible(shared_candidate,
                              transport_.local_endpoint_uid(), source_endpoint,
                              frame.destination_share_id)) {
      return {};
    }

    // Move source-share uniqueness validation into ImportValidatedNode BEFORE
    // CommitObjectGraph.
    int matching_shares = 0;
    for (auto const& share : shared_candidate.shares) {
      auto const* endpoint = ShareEndpoint(share);
      if (endpoint != nullptr && *endpoint == source_endpoint) {
        matching_share_id = share.share_id;
        ++matching_shares;
      }
    }
    if (matching_shares != 1 || !matching_share_id.is_valid()) {
      return {};
    }

    auto const source_sync_index =
        shared_candidate.FindLinkSyncIndexForShare(matching_share_id);
    if (source_sync_index >= shared_candidate.link_sync_states.size()) {
      return {};
    }
    if (!shared_candidate.link_sync_states[source_sync_index].is_valid()) {
      return {};
    }

    // A second node may name a Link this replica already imported. Only a
    // compatible descriptor is reused, and that decision is made before any
    // write. An incompatible ObjId still rejects the whole snapshot.
    if (!PlanReusableLinks(domain_, storage_, shared_candidate, parsed, chains,
                           reused_link_objects)) {
      return {};
    }
    if (reused_link_objects.count(frame.target_node_id) != 0) {
      return {};
    }
  }

  // Reject snapshot if any parsed object collides with receiver Domain or
  // storage. Objects of a compatible Link closure are not collisions: they
  // are left untouched, including the Link journal.
  for (auto const& [obj_id, classes] : parsed.state) {
    if (!classes.has_value()) {
      continue;
    }
    if (reused_link_objects.count(obj_id) != 0) {
      continue;
    }
    if (domain_.Find(obj_id)) {
      return {};
    }
    if (!storage_.Enumerate(obj_id).empty()) {
      return {};
    }
  }

  ae::RamDomainStorage admitted;
  for (auto const& [obj_id, classes] : parsed.state) {
    if (!classes.has_value()) {
      continue;
    }
    if (reused_link_objects.count(obj_id) != 0) {
      continue;
    }
    admitted.state.emplace(obj_id, classes);
  }
  CommitObjectGraph(admitted, storage_);
  auto node = SharedNode::ptr::Declare(
      ae::CreateWith{domain_}.with_id(frame.target_node_id));
  node.Load();
  assert(node.is_loaded() && "admitted snapshot must load from own storage");
  // Sender-local sync state is excluded from the snapshot by design.
  // Replaying the imported shared journal builds this replica's own
  // LinkSyncState for every Share, keyed by the relationship identity that
  // travelled with the topology.
  node->ReplayFromBase();
  if (node->shares.size() != 2) {
    return {};
  }
  return ImportedNode{
      .node = std::move(node),
      .source_share_id = matching_share_id,
  };
}

void SharedSyncRuntime::OnNodeState(std::string const& source_endpoint,
                                    NodeStateFrame const& frame) {
  auto node = FindNode(frame.target_node_id);
  bool const imported = !node.is_valid();
  ae::ObjId source_share_id;
  if (imported) {
    auto imported_result = ImportValidatedNode(source_endpoint, frame);
    if (!imported_result.node.is_valid()) {
      return;
    }
    node = std::move(imported_result.node);
    source_share_id = imported_result.source_share_id;
  } else if (!AddressedToThisReplica(*node, transport_.local_endpoint_uid(),
                                     source_endpoint,
                                     frame.destination_share_id)) {
    return;
  }

  LinkSyncState::ptr source_state;
  if (imported) {
    assert(source_share_id.is_valid());
    auto const source_sync_index =
        node->FindLinkSyncIndexForShare(source_share_id);
    assert(source_sync_index < node->link_sync_states.size());
    source_state = node->link_sync_states[source_sync_index].as_obj_ptr();
    if (!source_state.is_loaded()) {
      source_state.Load();
    }

    std::vector<SharedEventId> covered_ids;
    for (auto const& record : node->journal) {
      if (record.HasSharedIdentity() && !record.identity.origin_uid.empty()) {
        covered_ids.push_back(record.identity);
      }
    }

    source_state->CompleteFromReceivedSnapshot(std::move(covered_ids));
    source_state.Save();
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
      // Duplicate/complete ACK for the already-recorded packet is handled
      // below; rejoin fold is rejected entirely.
      return;
    }
    state->NoteInitialSyncReceived(frame.packet_id);
    node.Save();
    state.Save();
    if (source_state.is_valid()) {
      source_state.Save();
    }
  }

  // Registered once the snapshot is validated and durable. Binding may still
  // fail; Bound is stored only after the application callback accepts it.
  if (imported) {
    nodes_.push_back(node);
  }

  auto const expectation_index =
      MatchEndpointExpectation(source_endpoint, frame.target_node_id);
  bool const endpoint_expectation_open =
      expectation_index < expected_endpoint_nodes_.size();

  // Chat endpoint expectation: bind via application callback once.
  // Exact ExpectInitialNode alone does not require a callback.
  if (endpoint_expectation_open) {
    if (!initial_node_imported_callback_) {
      return;
    }
    bool const bound = initial_node_imported_callback_(source_endpoint, node);
    if (!bound) {
      return;
    }
    expected_endpoint_nodes_.erase(
        expected_endpoint_nodes_.begin() +
        static_cast<std::ptrdiff_t>(expectation_index));
  }

  QueueAck(source_endpoint,
           AckFrame{
               .packet_id = frame.packet_id,
               .target_node_id = frame.target_node_id,
               .destination_share_id = frame.destination_share_id,
           });
}

void SharedSyncRuntime::OnAck(std::string const& source_endpoint,
                              AckFrame const& frame) {
  auto node = FindNode(frame.target_node_id);
  if (!node.is_valid()) {
    return;
  }
  auto const* live = ShareEndpointOf(*node, frame.destination_share_id);
  if (live == nullptr || *live != source_endpoint) {
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
  if (state->pending_event_packet_id == frame.packet_id) {
    if (state->GetInitialSyncPhase() != InitialSyncPhase::Complete) {
      return;
    }
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

  // Already applied: acknowledge only when live addressing still authorizes
  // delivery. Identity/payload match alone is not authorization.
  if (auto const* existing = node->FindSharedEvent(frame.identity)) {
    auto event = existing->event;
    if (!event.is_valid()) {
      return;
    }
    if (!event.is_loaded()) {
      event.Load();
    }
    if (!event.is_loaded() ||
        existing->order.timestamp_us != frame.timestamp_us ||
        event->GetClassId() != frame.event_class_id ||
        !SameEventPayload(*event, frame.payload)) {
      return;
    }
    if (!EventAddressedToThisReplica(*node, transport_.local_endpoint_uid(),
                                     source_endpoint,
                                     frame.destination_share_id)) {
      return;
    }
    QueueAck(source_endpoint,
             AckFrame{
                 .packet_id = frame.packet_id,
                 .target_node_id = frame.target_node_id,
                 .destination_share_id = frame.destination_share_id,
             });
    return;
  }

  if (!EventAddressedToThisReplica(*node, transport_.local_endpoint_uid(),
                                   source_endpoint,
                                   frame.destination_share_id)) {
    return;
  }

  if (!IsStandaloneEventClassAllowed(frame.event_class_id)) {
    return;
  }

  ae::RamDomainStorage parsed;
  ae::ObjId wire_root_id;
  if (!ParseEventPayload(frame.payload, parsed, wire_root_id)) {
    return;
  }

  if (!ValidateStandaloneEventGraph(parsed, wire_root_id, frame.event_class_id)) {
    return;
  }

  {
    ae::RamDomainStorage scratch = parsed;
    ae::Domain scratch_domain{scratch};
    ae::DomainGraph scratch_graph{&scratch_domain};
    auto candidate = scratch_graph.LoadRoot(wire_root_id);
    if (!candidate) {
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

  // Preflight replay in a non-production scratch copy of the target SharedNode
  // at its historical insertion point.
  if (!PreflightHistoricalEventInsertion(*node, parsed, wire_root_id,
                                         frame.event_class_id, frame.identity,
                                         frame.timestamp_us)) {
    return;
  }

  auto local_event = ImportStandaloneEventGraph(
      parsed, wire_root_id, frame.event_class_id, domain_, storage_);
  if (!local_event) {
    return;
  }
  assert(local_event != nullptr &&
         "admitted Event must load from own storage");
  assert(local_event->GetClassId() == frame.event_class_id);

  auto local_event_ptr = Event::ptr::MakeFromThis(local_event.get());
  node->InsertShared(std::move(local_event_ptr), frame.identity,
                     SharedEventOrder{.timestamp_us = frame.timestamp_us});
  node.Save();

  QueueAck(source_endpoint,
           AckFrame{
               .packet_id = frame.packet_id,
               .target_node_id = frame.target_node_id,
               .destination_share_id = frame.destination_share_id,
           });
}

std::size_t SharedSyncRuntime::MatchEndpointExpectation(
    std::string const& source_endpoint, ae::ObjId node_id) const {
  std::size_t wildcard = expected_endpoint_nodes_.size();
  for (std::size_t i = 0; i < expected_endpoint_nodes_.size(); ++i) {
    auto const& exp = expected_endpoint_nodes_[i];
    if (exp.source_endpoint != source_endpoint) {
      continue;
    }
    if (exp.expected_node_id.is_valid()) {
      if (exp.expected_node_id == node_id) {
        return i;
      }
    } else if (wildcard == expected_endpoint_nodes_.size()) {
      wildcard = i;
    }
  }
  return wildcard;
}

void SharedSyncRuntime::SetAvailabilityWake(AvailabilityWake wake) {
  availability_wake_ = std::move(wake);
}

void SharedSyncRuntime::AvailabilityThunk(void* ctx,
                                          std::string const& endpoint,
                                          EndpointAvailability availability) {
  static_cast<SharedSyncRuntime*>(ctx)->OnAvailability(endpoint, availability);
}

void SharedSyncRuntime::OnAvailability(std::string const& endpoint,
                                       EndpointAvailability availability) {
  // The argument only names the endpoint. Availability() is the observation.
  (void)availability;
  auto const current = transport_.Availability(endpoint);
  EndpointAvailability previous = EndpointAvailability::Unknown;
  bool found = false;
  for (auto& observed : observed_availability_) {
    if (observed.endpoint == endpoint) {
      previous = observed.availability;
      observed.availability = current;
      found = true;
      break;
    }
  }
  if (!found) {
    observed_availability_.push_back(
        ObservedAvailability{.endpoint = endpoint, .availability = current});
  }
  if (found && previous == current) {
    return;
  }
  // Only Offline → Online pulls the retry clock forward. A repeated Online,
  // or Unknown → Online, keeps the existing interval. The stored previous
  // value detects that transition; it is not consulted by Send.
  if (previous == EndpointAvailability::Offline &&
      current == EndpointAvailability::Online) {
    ArmEndpoint(endpoint);
  }
  if (availability_wake_) {
    availability_wake_();
  }
}

void SharedSyncRuntime::ArmEndpoint(std::string const& endpoint) {
  for (auto& slot : sync_slots_) {
    auto node = FindNode(slot.node_id);
    if (!node.is_valid()) {
      continue;
    }
    auto const* live = ShareEndpointOf(*node, slot.share_id);
    if (live == nullptr || *live != endpoint) {
      continue;
    }
    if (slot.primed) {
      slot.next_us = 0;
    }
  }
}

bool SharedSyncRuntime::OutgoingOffline(std::string const& endpoint) const {
  return transport_.Availability(endpoint) == EndpointAvailability::Offline;
}

bool SharedSyncRuntime::TrySend(std::string const& endpoint,
                                std::vector<std::uint8_t> const& bytes) {
  if (endpoint.empty() || bytes.empty() || OutgoingOffline(endpoint)) {
    return false;
  }
  transport_.Send(endpoint, bytes);
  return true;
}

void SharedSyncRuntime::QueueAck(std::string const& endpoint,
                                 AckFrame const& frame) {
  auto bytes = EncodeAckFrame(frame);
  if (TrySend(endpoint, bytes)) {
    return;
  }
  for (auto const& pending : pending_acks_) {
    if (pending.endpoint == endpoint && pending.bytes == bytes) {
      return;
    }
  }
  pending_acks_.push_back(
      PendingAck{.endpoint = endpoint, .bytes = std::move(bytes)});
}

void SharedSyncRuntime::ServiceAcks() {
  if (pending_acks_.empty()) {
    return;
  }
  std::vector<PendingAck> waiting;
  waiting.reserve(pending_acks_.size());
  for (auto& pending : pending_acks_) {
    if (!TrySend(pending.endpoint, pending.bytes)) {
      waiting.push_back(std::move(pending));
    }
  }
  pending_acks_ = std::move(waiting);
}

std::vector<ae::ObjId> SharedSyncRuntime::RegisteredNodeIds() const {
  std::vector<ae::ObjId> ids;
  ids.reserve(nodes_.size());
  for (auto const& node : nodes_) {
    if (node.is_valid()) {
      ids.push_back(node.id());
    }
  }
  return ids;
}

void SharedSyncRuntime::ServiceShares(std::uint64_t now_us) {
  auto const& local = transport_.local_endpoint_uid();
  for (auto const& node : nodes_) {
    for (auto const& share : node->shares) {
      auto const* endpoint = ShareEndpoint(share);
      if (endpoint == nullptr || endpoint->empty() || *endpoint == local) {
        continue;
      }
      auto const destination = *endpoint;
      if (OutgoingOffline(destination)) {
        continue;
      }
      auto const sync_index =
          node->FindLinkSyncIndexForShare(share.share_id);
      if (sync_index >= node->link_sync_states.size()) {
        continue;
      }
      auto state = node->link_sync_states[sync_index];
      if (!state.is_loaded()) {
        state.Load();
      }
      auto const phase = state->GetInitialSyncPhase();
      bool const drive_snapshot =
          phase != InitialSyncPhase::Complete &&
          DrivesInitialSnapshot(*node, share.share_id);
      bool const drive_event = phase == InitialSyncPhase::Complete;
      if (!drive_snapshot && !drive_event) {
        continue;
      }

      SyncSlot* slot = nullptr;
      for (auto& existing : sync_slots_) {
        if (existing.node_id == node.id() &&
            existing.share_id == share.share_id) {
          slot = &existing;
          break;
        }
      }
      if (slot == nullptr) {
        sync_slots_.push_back(
            SyncSlot{.node_id = node.id(), .share_id = share.share_id});
        slot = &sync_slots_.back();
      }

      bool const is_retry = phase == InitialSyncPhase::Pending ||
                            state->HasPendingEvent();
      auto transmit = [&] {
        if (drive_snapshot) {
          SyncInitialState(node.id(), share.share_id);
        } else {
          SyncNextEvent(node.id(), share.share_id);
        }
      };

      if (!slot->primed) {
        transmit();
        slot->primed = true;
        slot->next_us = now_us + kShareOfferRetryIntervalUs;
        continue;
      }
      if (slot->next_us == kScheduleOnNextService) {
        slot->next_us = now_us + kShareOfferRetryIntervalUs;
        continue;
      }
      if (!is_retry || now_us >= slot->next_us) {
        transmit();
        slot->next_us = now_us + kShareOfferRetryIntervalUs;
      }
    }
  }
}

void SharedSyncRuntime::Service(std::uint64_t now_us) {
  if (now_us > logical_now_us_) {
    logical_now_us_ = now_us;
  }
  ServiceAcks();
  ServiceShares(now_us);
}

bool SharedSyncRuntime::LocalShareAllowsWrite(SharedNode const& node) const {
  auto const* self =
      ShareOfEndpoint(node, transport_.local_endpoint_uid());
  return self != nullptr && self->GetAccess() == ShareAccess::ReadWrite;
}

}  // namespace apptraverse
