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

bool TopologyEverKnewEndpoint(SharedNode const& node,
                              std::string const& endpoint_uid) {
  if (TopologyKnowsEndpoint(node, endpoint_uid)) {
    return true;
  }
  for (auto const& record : node.journal) {
    if (!record.event.is_valid()) {
      continue;
    }
    auto event = record.event;
    if (!event.is_loaded()) {
      event.Load();
    }
    if (!event.is_loaded() || event->GetClassId() != AddShareEvent::kClassId) {
      continue;
    }
    auto const& add = static_cast<AddShareEvent const&>(*event);
    if (!add.link.is_valid()) {
      continue;
    }
    if (!add.link.is_loaded()) {
      add.link.Load();
    }
    if (add.link.is_loaded() && add.link->EndpointUid() == endpoint_uid) {
      return true;
    }
  }
  return false;
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

bool FreezeTopologyPayload(Event const& event,
                           std::vector<std::uint8_t>& out) {
  if (event.GetClassId() != AddShareEvent::kClassId) {
    return FreezeEventPayload(event, out);
  }
  auto const& add = static_cast<AddShareEvent const&>(event);
  if (!add.link.is_valid()) {
    return false;
  }
  if (!add.link.is_loaded()) {
    add.link.Load();
  }
  if (!add.link.is_loaded()) {
    return false;
  }
  EventGraphExportBoundary boundary;
  boundary.Permit(event.obj_id);
  for (auto const id : SerializedClosure(*add.link)) {
    boundary.Permit(id);
  }
  return FreezeEventPayload(event, boundary, out);
}

Link::ptr AsLinkPtr(ae::Ptr<ae::Obj> held) {
  if (!held) {
    return {};
  }
  if (held->GetClassId() == MemoryLink::kClassId) {
    return MemoryLink::ptr::MakeFromThis(static_cast<MemoryLink*>(held.get()));
  }
  if (held->GetClassId() != Link::kClassId) {
    return {};
  }
  return Link::ptr::MakeFromThis(static_cast<Link*>(held.get()));
}

void ClearLinkJournal(Node& link) {
  link.journal.clear();
  for (Node* cursor = &link; cursor->base.is_valid();) {
    if (!cursor->base.is_loaded()) {
      cursor->base.Load();
    }
    cursor->base->journal.clear();
    cursor = &*cursor->base;
  }
}

std::vector<ae::ObjId> LinkBaseChain(Node& link) {
  std::vector<ae::ObjId> ids;
  ids.push_back(link.obj_id);
  for (Node* cursor = &link; cursor->base.is_valid();) {
    if (!cursor->base.is_loaded()) {
      cursor->base.Load();
    }
    ids.push_back(cursor->base.id());
    cursor = &*cursor->base;
  }
  return ids;
}

bool PreflightAddShare(SharedNode const& node, AddShareEvent& wire,
                       ae::RamDomainStorage const& link_bytes,
                       EventFrame const& frame) {
  ae::RamDomainStorage copy;
  BuildNetworkSharedScratch(node, copy);
  auto const ids = LinkBaseChain(static_cast<Node&>(*wire.link));
  for (auto const id : ids) {
    auto const existing = copy.state.find(id);
    if (existing != copy.state.end() && existing->second.has_value()) {
      continue;
    }
    auto const it = link_bytes.state.find(id);
    if (it == link_bytes.state.end() || !it->second.has_value()) {
      return false;
    }
    copy.state.emplace(id, it->second);
  }
  ae::Domain copy_domain{copy};
  ae::DomainGraph graph{&copy_domain};
  auto loaded = graph.LoadRoot(node.obj_id);
  if (!loaded) {
    return false;
  }
  // Keep the loaded Link alive. Domain stores only a weak reference, so a
  // discarded LoadRoot temporary is destroyed before Find can see it.
  auto link_root = graph.LoadRoot(wire.link.id());
  if (!link_root) {
    return false;
  }
  auto link = AsLinkPtr(link_root);
  if (!link.is_valid()) {
    return false;
  }
  auto local = AddShareEvent::ptr::Create(ae::CreateWith{copy_domain});
  local->link = std::move(link);
  local->access = wire.access;
  local->share_id = wire.share_id;
  bool const ok = static_cast<SharedNode&>(*loaded).TryInsertShared(
      std::move(local), frame.identity,
      SharedEventOrder{.timestamp_us = frame.timestamp_us});
  return ok;
}

// The endpoint named by an AddShare in the journal. Used after the share row
// is gone so a closing remove can still be addressed.
std::string EndpointIntroducedBy(SharedNode const& node, ae::ObjId share_id) {
  for (auto const& record : node.journal) {
    if (!record.event.is_valid()) {
      continue;
    }
    auto event = record.event;
    if (!event.is_loaded()) {
      event.Load();
    }
    if (!event.is_loaded() || event->GetClassId() != AddShareEvent::kClassId) {
      continue;
    }
    auto const& add = static_cast<AddShareEvent const&>(*event);
    if (add.share_id != share_id || !add.link.is_valid()) {
      continue;
    }
    if (!add.link.is_loaded()) {
      add.link.Load();
    }
    if (!add.link.is_loaded()) {
      return {};
    }
    return add.link->EndpointUid();
  }
  return {};
}

EventRecord const* LatestRemoveRecord(SharedNode const& node,
                                      ae::ObjId share_id) {
  EventRecord const* found = nullptr;
  for (auto const& record : node.journal) {
    if (!record.HasSharedIdentity() || !record.event.is_valid()) {
      continue;
    }
    auto event = record.event;
    if (!event.is_loaded()) {
      event.Load();
    }
    if (!event.is_loaded() ||
        event->GetClassId() != RemoveShareEvent::kClassId) {
      continue;
    }
    if (static_cast<RemoveShareEvent const&>(*event).share_id == share_id) {
      found = &record;
    }
  }
  return found;
}

bool ShareIntroducedBySharedEvent(SharedNode const& node, ae::ObjId share_id) {
  // Catch-up is only for a relationship that already travelled as a shared
  // event. A local AddShare is still waiting for an offer to carry the
  // snapshot; starting catch-up first would freeze that slot on the wrong packet.
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
    return record.HasSharedIdentity();
  }
  return false;
}

// Live RW relationship, or a closed relationship that ended here and whose
// source was a known participant. Matching identity/payload alone is not
// enough for a foreign endpoint.
bool MayAcknowledgeDelivery(SharedNode const& node,
                            std::string const& local_endpoint,
                            std::string const& source_endpoint,
                            ae::ObjId destination_share_id) {
  if (EventAddressedToThisReplica(node, local_endpoint, source_endpoint,
                                  destination_share_id)) {
    return true;
  }
  if (source_endpoint.empty() || source_endpoint == local_endpoint) {
    return false;
  }
  std::string destination;
  if (auto const* live = ShareEndpointOf(node, destination_share_id)) {
    destination = *live;
  } else {
    destination = EndpointIntroducedBy(node, destination_share_id);
  }
  if (destination != local_endpoint) {
    return false;
  }
  return TopologyEverKnewEndpoint(node, source_endpoint);
}

}  // namespace

SharedSyncRuntime::SharedSyncRuntime(ae::Domain& domain,
                                     ae::IDomainStorage& storage,
                                     IByteTransport& transport)
    : domain_{domain}, storage_{storage}, transport_{transport} {
  transport_.BindReceive(this, &SharedSyncRuntime::ReceiveThunk);
  transport_.BindAvailability(this, &SharedSyncRuntime::AvailabilityThunk);
  LoadAdmission();
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
  RelayAppliedRemovals(nodes_.back());
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
  if (ae::Registry::GetRegistry().GenerationDistance(Event::kClassId, class_id) < 0) {
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
      EventFrame pending;
      if (!DecodeEventFrame(state->pending_event_packet, pending) ||
          !IsTopologyEventClass(pending.event_class_id)) {
        return;
      }
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

  bool const topology = IsTopologyEventClass(record->event->GetClassId());
  if (!topology && !IsStandaloneEventClassAllowed(record->event->GetClassId())) {
    return;
  }
  if (!topology && !LocalShareAllowsWrite(*node)) {
    return;
  }

  std::vector<std::uint8_t> payload;
  if (!FreezeTopologyPayload(*record->event, payload)) {
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
    case SyncFrameType::kShareOffer: {
      ShareOfferFrame frame;
      if (DecodeShareOfferFrame(bytes, frame)) {
        OnShareOffer(source_endpoint, frame);
      }
      break;
    }
    case SyncFrameType::kShareRequest: {
      ShareOfferFrame frame;
      if (DecodeShareRequestFrame(bytes, frame)) {
        OnShareRequest(source_endpoint, frame);
      }
      break;
    }
    case SyncFrameType::kShareDecision: {
      ShareDecisionFrame frame;
      if (DecodeShareDecisionFrame(bytes, frame)) {
        OnShareDecision(source_endpoint, frame);
      }
      break;
    }
    case SyncFrameType::kShareCatchUp: {
      ShareCatchUpFrame frame;
      if (DecodeShareCatchUpFrame(bytes, frame)) {
        OnCatchUp(source_endpoint, frame);
      }
      break;
    }
  }
}

SharedSyncRuntime::ImportedNode SharedSyncRuntime::ImportValidatedNode(
    std::string const& source_endpoint, NodeStateFrame const& frame) {
  // Untrusted bytes may not create arbitrary roots. Permission is either a
  // persisted share offer for this source and node, an exact node id this
  // replica is waiting for, or one matching endpoint expectation.
  auto const admission = FindImportAdmission(source_endpoint, frame.target_node_id);
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

  if (!admission.is_valid() && !exact_expected && !endpoint_expected) {
    return {};
  }

  ae::RamDomainStorage parsed;
  if (!DeserializeObjectGraph(frame.payload, parsed)) {
    return {};
  }

  // Validate the parsed object graph's stored class chains BEFORE loading any candidate object.
  std::vector<StoredClassChainInfo> chains;
  if (!ValidateStoredClassChains(parsed, &chains)) {
    return {};
  }

  // Ensure root target object is in the payload and its most-derived class derives SharedNode.
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
      if (admission.is_valid() &&
          chain_info.most_derived_class_id != admission->root_class_id) {
        return {};
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
    if (admission.is_valid()) {
      auto const dest_index = shared_candidate.FindShareIndexForShare(
          frame.destination_share_id);
      if (dest_index >= shared_candidate.shares.size() ||
          shared_candidate.shares[dest_index].GetAccess() !=
              admission->GetAccess()) {
        // The agreed right is part of the offer. A snapshot that grants a
        // different right is not that offer.
        return {};
      }
    }

    // Move source-share uniqueness validation into ImportValidatedNode BEFORE CommitObjectGraph.
    // 1. In the SCRATCH candidate, iterate candidate.shares.
    // 2. For each Share: resolve its Link, compare EndpointUid() with source_endpoint.
    // 3. Require EXACTLY ONE matching Share.
    // 4. Require matching share_id valid.
    // 5. Require candidate.FindLinkSyncIndexForShare(matching_share_id)
    //    identifies exactly one local LinkSyncState after replay.
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
  return ImportedNode{
      .node = std::move(node),
      .source_share_id = matching_share_id,
  };
}

void SharedSyncRuntime::OnNodeState(std::string const& source_endpoint,
                                    NodeStateFrame const& frame) {
  auto node = FindNode(frame.target_node_id);
  bool const imported = !node.is_valid();
  auto admission = FindImportAdmission(source_endpoint, frame.target_node_id);
  ae::ObjId source_share_id;
  if (imported) {
    auto imported_result = ImportValidatedNode(source_endpoint, frame);
    if (!imported_result.node.is_valid()) {
      return;
    }
    node = std::move(imported_result.node);
    source_share_id = imported_result.source_share_id;
  } else if (admission.is_valid() &&
             admission->GetPhase() == ShareOfferPhase::Admitted &&
             node->FindShareIndexForShare(frame.destination_share_id) >=
                 node->shares.size()) {
// Re-join: local X already exists. Fold missing shared events; do not
    // replace the node or wipe its history.
    if (!FoldMissingSharedFromSnapshot(node, source_endpoint, frame,
                                       admission)) {
return;
    }
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
    if (state->received_initial_packet_id.is_valid() ||
        (!imported &&
         !(admission.is_valid() &&
           admission->GetPhase() == ShareOfferPhase::Admitted))) {
      // A second, different initial snapshot is not part of protocol v1.
      // Re-join under an open admission may record the new relationship's
      // first packet after folding missing shared events.
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
    RelayAppliedRemovals(node);
  }

  bool const admission_needs_bind =
      admission.is_valid() &&
      admission->GetPhase() == ShareOfferPhase::Admitted;

  auto const expectation_index =
      MatchEndpointExpectation(source_endpoint, frame.target_node_id);
  bool const endpoint_expectation_open =
      expectation_index < expected_endpoint_nodes_.size();

  // One callback covers both the admission path and the chat endpoint
  // expectation. A repeat after Bound does not bind again.
  if (admission_needs_bind || endpoint_expectation_open) {
    if (!initial_node_imported_callback_) {
      return;
    }
    bool const bound = initial_node_imported_callback_(source_endpoint, node);
    if (!bound) {
      return;
    }
    if (admission_needs_bind) {
      CommitOfferPhase(admission, ShareOfferPhase::Bound,
                       frame.destination_share_id);
      SaveOffer(admission);
    }
    if (endpoint_expectation_open) {
      expected_endpoint_nodes_.erase(expected_endpoint_nodes_.begin() +
                                     static_cast<std::ptrdiff_t>(
                                         expectation_index));
    }
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
  // Live share or closed relationship whose Link still names this source.
  std::string destination;
  if (auto const* live =
          ShareEndpointOf(*node, frame.destination_share_id)) {
    destination = *live;
  } else {
    destination = EndpointIntroducedBy(*node, frame.destination_share_id);
  }
  if (destination != source_endpoint) {
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
    MarkSnapshotSenderComplete(frame.target_node_id, frame.destination_share_id);
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

  // Already applied: acknowledge only with a live or historical delivery
  // context. Identity/payload match alone is not authorization.
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
    if (!MayAcknowledgeDelivery(*node, transport_.local_endpoint_uid(),
                                source_endpoint, frame.destination_share_id)) {
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

  if (frame.event_class_id == AddShareEvent::kClassId) {
    if (!ApplyIncomingAddShare(node, source_endpoint, frame)) {
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

  if (!IsTopologyEventClass(frame.event_class_id) &&
      !IsStandaloneEventClassAllowed(frame.event_class_id)) {
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
                                         frame.event_class_id,
                                         frame.identity,
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
  node->InsertShared(
      std::move(local_event_ptr), frame.identity,
      SharedEventOrder{.timestamp_us = frame.timestamp_us});
  node.Save();

  if (frame.event_class_id == RemoveShareEvent::kClassId) {
    RelayRemovedShare(
        node, static_cast<RemoveShareEvent const&>(*local_event).share_id);
  }

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

ShareOffer::ptr SharedSyncRuntime::FindOfferByOperation(
    ae::ObjId operation_id) const {
  for (auto const& live : offers_) {
    if (live.offer.is_valid() && live.offer->operation_id == operation_id) {
      return live.offer;
    }
  }
  return ShareOffer::ptr{};
}

ShareOffer::ptr SharedSyncRuntime::FindImportAdmission(
    std::string const& source_endpoint, ae::ObjId node_id) const {
  ShareOffer::ptr bound;
  for (auto const& live : offers_) {
    if (!live.offer.is_valid()) {
      continue;
    }
    auto const& offer = *live.offer;
    if (offer.remote_endpoint != source_endpoint || offer.node_id != node_id) {
      continue;
    }
    if (offer.GetPhase() != ShareOfferPhase::Admitted &&
        offer.GetPhase() != ShareOfferPhase::Bound) {
      continue;
    }
    bool const importer =
        (offer.GetKind() == ShareOfferKind::Grant &&
         offer.GetRole() == ShareOfferRole::Responder) ||
        (offer.GetKind() == ShareOfferKind::Request &&
         offer.GetRole() == ShareOfferRole::Initiator);
    if (!importer) {
      continue;
    }
    // An open Admitted attempt must win over a finished Bound of a closed
    // relationship; otherwise re-join cannot fold into the existing node.
    if (offer.GetPhase() == ShareOfferPhase::Admitted) {
      return live.offer;
    }
    if (offer.GetPhase() == ShareOfferPhase::Bound && offer.share_id.is_valid()) {
      auto node = FindNode(node_id);
      if (node.is_valid() &&
          node->FindShareIndexForShare(offer.share_id) < node->shares.size()) {
        bound = live.offer;
      }
    }
  }
  return bound;
}

bool SharedSyncRuntime::OpenAttemptBlocks(std::string const& remote_endpoint,
                                          ae::ObjId node_id) const {
  for (auto const& live : offers_) {
    if (!live.offer.is_valid()) {
      continue;
    }
    auto const& offer = *live.offer;
    if (offer.remote_endpoint != remote_endpoint || offer.node_id != node_id) {
      continue;
    }
    if (offer.GetPhase() == ShareOfferPhase::Rejected ||
        offer.GetPhase() == ShareOfferPhase::Unset) {
      continue;
    }
    // A finished operation blocks only while that relationship is still live.
    // After remove, the same endpoint may open a new attempt with a new id.
    if (offer.GetPhase() == ShareOfferPhase::Complete ||
        offer.GetPhase() == ShareOfferPhase::Bound) {
      if (!offer.share_id.is_valid()) {
        continue;
      }
      auto node = FindNode(node_id);
      if (node.is_valid() &&
          node->FindShareIndexForShare(offer.share_id) < node->shares.size()) {
        return true;
      }
      continue;
    }
    return true;
  }
  return false;
}

bool SharedSyncRuntime::AttemptMatches(ShareOffer const& offer,
                                       std::string const& source,
                                       ShareOfferFrame const& frame,
                                       ShareOfferKind kind) const {
  return offer.GetRole() == ShareOfferRole::Responder &&
         offer.GetKind() == kind && offer.remote_endpoint == source &&
         offer.node_id == frame.target_node_id &&
         offer.requested_class == frame.root_class_id &&
         offer.requested_access == frame.access;
}

SharedSyncRuntime::ShareOfferView SharedSyncRuntime::ViewOf(
    ShareOffer const& offer) const {
  return ShareOfferView{
      .source_endpoint = offer.remote_endpoint,
      .operation_id = offer.operation_id,
      .node_id = offer.node_id,
      .root_class_id = offer.requested_class,
      .access = offer.GetRequestedAccess(),
      .kind = offer.GetKind(),
  };
}

void SharedSyncRuntime::CommitOfferPhase(ShareOffer::ptr offer,
                                         ShareOfferPhase phase,
                                         ae::ObjId share_id,
                                         std::vector<std::uint8_t> packet,
                                         std::uint8_t access,
                                         std::uint32_t root_class_id) {
  auto event =
      SetShareOfferPhaseEvent::ptr::Create(ae::CreateWith{domain_});
  event->phase = static_cast<std::uint8_t>(phase);
  event->share_id = share_id;
  event->packet = std::move(packet);
  event->access = access;
  event->root_class_id = root_class_id;
  assert(offer->CanApply(*event));
  offer->Commit(event);
}

void SharedSyncRuntime::SaveOffer(ShareOffer::ptr offer) {
  offer.Save();
  if (offer->remote_link.is_valid()) {
    if (!offer->remote_link.is_loaded()) {
      offer->remote_link.Load();
    }
    offer->remote_link.Save();
  }
}

void SharedSyncRuntime::RememberOffer(ShareOffer::ptr offer, bool send_now) {
  if (!admission_.is_valid()) {
    if (!storage_.Enumerate(kShareAdmissionRootId).empty()) {
      admission_ = ShareAdmission::ptr::Declare(
          ae::CreateWith{domain_}.with_id(kShareAdmissionRootId));
      admission_.Load();
      assert(admission_.is_loaded());
    } else {
      admission_ = ShareAdmission::ptr::Create(
          ae::CreateWith{domain_}.with_id(kShareAdmissionRootId));
      InitializeRuntimeNode(*admission_);
      admission_.Save();
    }
  }
  bool attached = false;
  for (auto const& existing : admission_->offers) {
    if (existing.is_valid() && existing.id() == offer.id()) {
      attached = true;
      break;
    }
  }
  if (!attached) {
    auto event =
        AttachShareOfferEvent::ptr::Create(ae::CreateWith{domain_});
    event->offer = offer;
    admission_->Commit(event);
    admission_.Save();
  }
  auto const endpoint = offer->remote_endpoint;
  auto const packet = offer->pending_packet;
  bool const handed =
      send_now && !packet.empty() && TrySend(endpoint, packet);
  offers_.push_back(LiveOffer{
      .offer = std::move(offer),
      .next_send_us = handed ? kScheduleOnNextService : 0,
      .primed = handed,
  });
}

void SharedSyncRuntime::LoadAdmission() {
  if (storage_.Enumerate(kShareAdmissionRootId).empty()) {
    return;
  }
  admission_ = ShareAdmission::ptr::Declare(
      ae::CreateWith{domain_}.with_id(kShareAdmissionRootId));
  admission_.Load();
  assert(admission_.is_loaded() && "stored ShareAdmission must load");
  for (auto& offer : admission_->offers) {
    if (!offer.is_valid()) {
      continue;
    }
    if (!offer.is_loaded()) {
      offer.Load();
    }
    assert(offer.is_loaded());
    RegisterOffer(offer);
  }
}

void SharedSyncRuntime::NoteDirectSend(ae::ObjId operation_id) {
  for (auto& live : offers_) {
    if (live.offer.is_valid() && live.offer->operation_id == operation_id) {
      live.primed = true;
      live.next_send_us = kScheduleOnNextService;
      live.decision_unsent = false;
      return;
    }
  }
}

void SharedSyncRuntime::NoteDirectSync(ae::ObjId node_id, ae::ObjId share_id) {
  for (auto& slot : sync_slots_) {
    if (slot.node_id == node_id && slot.share_id == share_id) {
      slot.primed = true;
      slot.next_us = kScheduleOnNextService;
      return;
    }
  }
  sync_slots_.push_back(SyncSlot{
      .node_id = node_id,
      .share_id = share_id,
      .next_us = kScheduleOnNextService,
      .primed = true,
  });
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
  for (auto& live : offers_) {
    if (!live.offer.is_valid() || live.offer->remote_endpoint != endpoint) {
      continue;
    }
    if (live.primed) {
      live.next_send_us = 0;
    }
  }
  for (auto& slot : sync_slots_) {
    auto node = FindNode(slot.node_id);
    if (!node.is_valid()) {
      continue;
    }
    std::string destination;
    if (auto const* live = ShareEndpointOf(*node, slot.share_id)) {
      destination = *live;
    } else {
      // Closed relationship: LinkSyncState still drives remove delivery.
      destination = EndpointIntroducedBy(*node, slot.share_id);
    }
    if (destination != endpoint) {
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

void SharedSyncRuntime::ServiceRelays(std::uint64_t now_us) {
  for (auto& node : nodes_) {
    if (!node.is_valid() || !node.is_loaded()) {
      continue;
    }
    for (auto& entry : node->link_sync_states) {
      if (!entry.is_valid()) {
        continue;
      }
      if (!entry.is_loaded()) {
        entry.Load();
      }
      if (!entry.is_loaded() || !entry->HasPendingEvent()) {
        continue;
      }
      // Open shares are driven by ServiceShares. Closed ones only appear here.
      if (node->FindShareIndexForShare(entry->share_id) < node->shares.size()) {
        continue;
      }
      auto const endpoint = EndpointIntroducedBy(*node, entry->share_id);
      if (endpoint.empty() || endpoint == transport_.local_endpoint_uid()) {
        continue;
      }
      if (OutgoingOffline(endpoint)) {
        continue;
      }
      SyncSlot* slot = nullptr;
      for (auto& existing : sync_slots_) {
        if (existing.node_id == node.id() &&
            existing.share_id == entry->share_id) {
          slot = &existing;
          break;
        }
      }
      if (slot == nullptr) {
        sync_slots_.push_back(
            SyncSlot{.node_id = node.id(), .share_id = entry->share_id});
        slot = &sync_slots_.back();
      }
      auto transmit = [&] {
        TrySend(endpoint, entry->pending_event_packet);
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
      if (now_us >= slot->next_us) {
        transmit();
        slot->next_us = now_us + kShareOfferRetryIntervalUs;
      }
    }
  }
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

void SharedSyncRuntime::MarkDecisionUnsent(ae::ObjId operation_id) {
  for (auto& live : offers_) {
    if (live.offer.is_valid() && live.offer->operation_id == operation_id) {
      live.decision_unsent = true;
      return;
    }
  }
}

void SharedSyncRuntime::MarkSnapshotSenderComplete(ae::ObjId node_id,
                                                   ae::ObjId share_id) {
  for (auto const& live : offers_) {
    if (!live.offer.is_valid()) {
      continue;
    }
    auto offer = live.offer;
    if (offer->node_id != node_id || offer->share_id != share_id) {
      continue;
    }
    if (!SendsSnapshot(*offer) ||
        offer->GetPhase() != ShareOfferPhase::Accepted) {
      continue;
    }
    CommitOfferPhase(offer, ShareOfferPhase::Complete, share_id);
    SaveOffer(offer);
  }
}

bool SharedSyncRuntime::SendsSnapshot(ShareOffer const& offer) const {
  auto const phase = offer.GetPhase();
  if (phase != ShareOfferPhase::Accepted &&
      phase != ShareOfferPhase::Complete) {
    return false;
  }
  if (offer.GetKind() == ShareOfferKind::Grant) {
    return offer.GetRole() == ShareOfferRole::Initiator;
  }
  return offer.GetRole() == ShareOfferRole::Responder;
}

bool SharedSyncRuntime::DrivesInitialSnapshot(ae::ObjId node_id,
                                              ae::ObjId share_id) const {
  for (auto const& live : offers_) {
    if (!live.offer.is_valid()) {
      continue;
    }
    auto const& offer = *live.offer;
    if (offer.node_id != node_id || offer.share_id != share_id) {
      continue;
    }
    if (SendsSnapshot(offer)) {
      return true;
    }
  }
  return false;
}

void SharedSyncRuntime::SetShareOfferPolicy(ShareOfferPolicy policy) {
  share_offer_policy_ = std::move(policy);
}

void SharedSyncRuntime::SetShareOfferNotice(ShareOfferNotice notice) {
  share_offer_notice_ = std::move(notice);
  if (!share_offer_notice_) {
    return;
  }
  for (auto const& live : offers_) {
    if (!live.offer.is_valid()) {
      continue;
    }
    if (live.offer->GetPhase() == ShareOfferPhase::AwaitingDecision) {
      share_offer_notice_(ViewOf(*live.offer));
    }
  }
}

void SharedSyncRuntime::SetLinkForEndpoint(LinkForEndpoint link_for_endpoint) {
  link_for_endpoint_ = std::move(link_for_endpoint);
}

ae::ObjId SharedSyncRuntime::OfferNode(SharedNode::ptr node, Link::ptr remote,
                                       ShareAccess access) {
  assert(node.is_valid() && node.is_loaded());
  assert(FindNode(node.id()).is_valid() &&
         "OfferNode requires a registered SharedNode");
  assert(remote.is_valid() && remote.is_loaded());
  assert(!remote->EndpointUid().empty());
  assert(remote->EndpointUid() != transport_.local_endpoint_uid() &&
         "an offer to this replica's own endpoint is not a remote share");

  bool has_local = false;
  for (auto const& share : node->shares) {
    auto const* endpoint = ShareEndpoint(share);
    if (endpoint != nullptr &&
        *endpoint == transport_.local_endpoint_uid()) {
      has_local = true;
      break;
    }
  }
  assert(has_local &&
         "OfferNode requires the node to already share this replica");

  for (auto const& live : offers_) {
    if (!live.offer.is_valid()) {
      continue;
    }
    auto const& offer = *live.offer;
    if (offer.GetRole() != ShareOfferRole::Initiator ||
        offer.GetKind() != ShareOfferKind::Grant) {
      continue;
    }
    if (offer.node_id != node.id() ||
        offer.remote_endpoint != remote->EndpointUid()) {
      continue;
    }
    if (offer.GetPhase() == ShareOfferPhase::Rejected ||
        offer.GetPhase() == ShareOfferPhase::Unset) {
      continue;
    }
    if (offer.GetPhase() == ShareOfferPhase::Complete ||
        offer.GetPhase() == ShareOfferPhase::Bound) {
      // Finished grant: reuse only while that relationship share is still live.
      if (offer.share_id.is_valid() &&
          node->FindShareIndexForShare(offer.share_id) < node->shares.size()) {
        return offer.operation_id;
      }
      continue;
    }
    return offer.operation_id;
  }

  auto offer = ShareOffer::ptr::Create(ae::CreateWith{domain_});
  InitializeRuntimeNode(*offer);
  auto event = OpenShareOfferEvent::ptr::Create(ae::CreateWith{domain_});
  ShareOfferFrame const frame{
      .packet_id = event.id(),
      .operation_id = offer.id(),
      .target_node_id = node.id(),
      .root_class_id = node->GetClassId(),
      .access = static_cast<std::uint8_t>(access),
  };
  event->role = static_cast<std::uint8_t>(ShareOfferRole::Initiator);
  event->phase = static_cast<std::uint8_t>(ShareOfferPhase::Pending);
  event->kind = static_cast<std::uint8_t>(ShareOfferKind::Grant);
  event->node_id = node.id();
  event->root_class_id = node->GetClassId();
  event->remote_endpoint = remote->EndpointUid();
  event->access = static_cast<std::uint8_t>(access);
  event->requested_access = static_cast<std::uint8_t>(access);
  event->requested_class = node->GetClassId();
  event->remote_link = std::move(remote);
  event->operation_id = offer.id();
  event->packet = EncodeShareOfferFrame(frame);
  offer->Commit(event);
  SaveOffer(offer);
  RememberOffer(offer, true);
  return offer->operation_id;
}

ae::ObjId SharedSyncRuntime::RequestJoin(std::string remote_endpoint,
                                         ae::ObjId node_id,
                                         ShareAccess requested_access) {
  assert(node_id.is_valid());
  assert(!remote_endpoint.empty());
  assert(remote_endpoint != transport_.local_endpoint_uid());
  assert(requested_access == ShareAccess::ReadWrite ||
         requested_access == ShareAccess::ReadOnly);

  for (auto const& live : offers_) {
    if (!live.offer.is_valid()) {
      continue;
    }
    auto const& offer = *live.offer;
    if (offer.GetRole() != ShareOfferRole::Initiator ||
        offer.GetKind() != ShareOfferKind::Request) {
      continue;
    }
    if (offer.node_id != node_id || offer.remote_endpoint != remote_endpoint) {
      continue;
    }
    if (offer.GetPhase() == ShareOfferPhase::Rejected ||
        offer.GetPhase() == ShareOfferPhase::Unset) {
      continue;
    }
    if (offer.GetPhase() == ShareOfferPhase::Complete ||
        offer.GetPhase() == ShareOfferPhase::Bound) {
      // Finished request: reuse only while that relationship share is still live.
      auto node = FindNode(node_id);
      if (node.is_valid() && offer.share_id.is_valid() &&
          node->FindShareIndexForShare(offer.share_id) < node->shares.size()) {
        return offer.operation_id;
      }
      continue;
    }
    return offer.operation_id;
  }

  auto offer = ShareOffer::ptr::Create(ae::CreateWith{domain_});
  InitializeRuntimeNode(*offer);
  auto event = OpenShareOfferEvent::ptr::Create(ae::CreateWith{domain_});
  ShareOfferFrame const frame{
      .packet_id = event.id(),
      .operation_id = offer.id(),
      .target_node_id = node_id,
      .root_class_id = 0,
      .access = static_cast<std::uint8_t>(requested_access),
  };
  event->role = static_cast<std::uint8_t>(ShareOfferRole::Initiator);
  event->phase = static_cast<std::uint8_t>(ShareOfferPhase::Pending);
  event->kind = static_cast<std::uint8_t>(ShareOfferKind::Request);
  event->node_id = node_id;
  event->root_class_id = 0;
  event->remote_endpoint = remote_endpoint;
  event->access = static_cast<std::uint8_t>(requested_access);
  event->requested_access = static_cast<std::uint8_t>(requested_access);
  event->requested_class = 0;
  event->operation_id = offer.id();
  event->packet = EncodeShareRequestFrame(frame);
  offer->Commit(event);
  SaveOffer(offer);
  RememberOffer(offer, true);
  return offer->operation_id;
}

void SharedSyncRuntime::AcceptJoin(ae::ObjId operation_id,
                                   ShareAccess granted_access,
                                   Link::ptr remote) {
  auto offer = FindOfferByOperation(operation_id);
  if (!offer.is_valid() || offer->GetRole() != ShareOfferRole::Responder ||
      offer->GetPhase() != ShareOfferPhase::AwaitingDecision) {
    return;
  }
  assert(granted_access == ShareAccess::ReadWrite ||
         granted_access == ShareAccess::ReadOnly);

  std::uint32_t class_id = offer->root_class_id;
  ae::ObjId share_id;
  auto next = ShareOfferPhase::Admitted;
  if (offer->GetKind() == ShareOfferKind::Grant) {
    if (static_cast<std::uint8_t>(granted_access) != offer->requested_access) {
      return;
    }
  } else {
    if (!remote.is_valid() && link_for_endpoint_) {
      remote = link_for_endpoint_(offer->remote_endpoint);
    }
    if (!remote.is_valid() || !remote.is_loaded()) {
      return;
    }
    if (remote->EndpointUid() != offer->remote_endpoint) {
      return;
    }
    auto node = FindNode(offer->node_id);
    if (!node.is_valid()) {
      return;
    }
    if (offer->requested_class != 0 &&
        offer->requested_class != node->GetClassId()) {
      return;
    }
    class_id = node->GetClassId();
    if (node->FindShareIndex(remote.id()) >= node->shares.size()) {
      PublishAddShare(node, remote, granted_access);
    }
    auto const share_index = node->FindShareIndex(remote.id());
    assert(share_index < node->shares.size());
    if (node->shares[share_index].GetAccess() != granted_access) {
      PublishShareAccess(node, node->shares[share_index].share_id,
                         granted_access);
    }
    share_id = node->shares[share_index].share_id;
    node.Save();
    for (auto& entry : node->link_sync_states) {
      entry.Save();
    }
    next = ShareOfferPhase::Accepted;
  }

  auto event = SetShareOfferPhaseEvent::ptr::Create(ae::CreateWith{domain_});
  ShareDecisionFrame const decision{
      .packet_id = event.id(),
      .operation_id = offer->operation_id,
      .target_node_id = offer->node_id,
      .root_class_id = class_id == 0 ? 1 : class_id,
      .access = static_cast<std::uint8_t>(granted_access),
      .accepted = true,
  };
  event->phase = static_cast<std::uint8_t>(next);
  event->share_id = share_id;
  event->packet = EncodeShareDecisionFrame(decision);
  event->access = static_cast<std::uint8_t>(granted_access);
  event->root_class_id = class_id;
  assert(offer->CanApply(*event));
  offer->Commit(event);
  SaveOffer(offer);
  if (TrySend(offer->remote_endpoint, offer->pending_packet)) {
    NoteDirectSend(offer->operation_id);
  } else {
    MarkDecisionUnsent(offer->operation_id);
  }
  if (next == ShareOfferPhase::Accepted) {
    SyncInitialState(offer->node_id, share_id);
    if (!OutgoingOffline(offer->remote_endpoint)) {
      NoteDirectSync(offer->node_id, share_id);
    }
  }
}

void SharedSyncRuntime::RejectJoin(ae::ObjId operation_id) {
  auto offer = FindOfferByOperation(operation_id);
  if (!offer.is_valid() || offer->GetRole() != ShareOfferRole::Responder ||
      offer->GetPhase() != ShareOfferPhase::AwaitingDecision) {
    return;
  }
  auto const class_id =
      offer->root_class_id == 0 ? std::uint32_t{1} : offer->root_class_id;
  auto event = SetShareOfferPhaseEvent::ptr::Create(ae::CreateWith{domain_});
  ShareDecisionFrame const decision{
      .packet_id = event.id(),
      .operation_id = offer->operation_id,
      .target_node_id = offer->node_id,
      .root_class_id = class_id,
      .access = offer->requested_access,
      .accepted = false,
  };
  event->phase = static_cast<std::uint8_t>(ShareOfferPhase::Rejected);
  event->packet = EncodeShareDecisionFrame(decision);
  assert(offer->CanApply(*event));
  offer->Commit(event);
  SaveOffer(offer);
  if (TrySend(offer->remote_endpoint, offer->pending_packet)) {
    NoteDirectSend(offer->operation_id);
  } else {
    MarkDecisionUnsent(offer->operation_id);
  }
}

void SharedSyncRuntime::RegisterOffer(ShareOffer::ptr offer) {
  assert(offer.is_valid() && offer.is_loaded());
  assert(offer->operation_id.is_valid());
  for (auto const& live : offers_) {
    if (live.offer.is_valid() && live.offer.id() == offer.id()) {
      assert(false && "ShareOffer registered twice");
    }
  }
  offers_.push_back(LiveOffer{.offer = offer});
  if (!offer->node_id.is_valid() || FindNode(offer->node_id).is_valid()) {
    return;
  }
  if (storage_.Enumerate(offer->node_id).empty()) {
    return;
  }
  auto node = SharedNode::ptr::Declare(
      ae::CreateWith{domain_}.with_id(offer->node_id));
  node.Load();
  assert(node.is_loaded() && "stored SharedNode must load");
  RegisterNode(node);
}

std::vector<SharedSyncRuntime::ShareOfferStatus>
SharedSyncRuntime::OfferStatuses() const {
  std::vector<ShareOfferStatus> out;
  out.reserve(offers_.size());
  for (auto const& live : offers_) {
    if (!live.offer.is_valid()) {
      continue;
    }
    auto const& offer = *live.offer;
    out.push_back(ShareOfferStatus{
        .object_id = live.offer.id(),
        .operation_id = offer.operation_id,
        .node_id = offer.node_id,
        .remote_endpoint = offer.remote_endpoint,
        .role = offer.GetRole(),
        .phase = offer.GetPhase(),
        .access = offer.GetAccess(),
    });
  }
  return out;
}

ShareOfferPhase SharedSyncRuntime::OfferPhase(ae::ObjId operation_id) const {
  auto offer = FindOfferByOperation(operation_id);
  if (!offer.is_valid()) {
    return ShareOfferPhase::Unset;
  }
  return offer->GetPhase();
}

std::vector<ae::ObjId> SharedSyncRuntime::LocalOfferIds() const {
  std::vector<ae::ObjId> ids;
  ids.reserve(offers_.size());
  for (auto const& live : offers_) {
    if (live.offer.is_valid()) {
      ids.push_back(live.offer.id());
    }
  }
  return ids;
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

void SharedSyncRuntime::ServiceOffers(std::uint64_t now_us) {
  for (auto& live : offers_) {
    if (!live.offer.is_valid()) {
      continue;
    }
    auto const endpoint = live.offer->remote_endpoint;
    if (endpoint.empty()) {
      continue;
    }
    // Offline keeps the persisted packet and does not move the retry clock,
    // so a later time jump cannot flush a burst of sends.
    if (OutgoingOffline(endpoint)) {
      continue;
    }

    if (live.decision_unsent && !live.offer->pending_packet.empty()) {
      if (TrySend(endpoint, live.offer->pending_packet)) {
        live.decision_unsent = false;
        live.primed = true;
        live.next_send_us = now_us + kShareOfferRetryIntervalUs;
      }
    }

    bool const due = live.offer->GetRole() == ShareOfferRole::Initiator &&
                     live.offer->GetPhase() == ShareOfferPhase::Pending &&
                     !live.offer->pending_packet.empty();
    if (!due) {
      continue;
    }
    if (!live.primed) {
      if (TrySend(endpoint, live.offer->pending_packet)) {
        live.primed = true;
        live.next_send_us = now_us + kShareOfferRetryIntervalUs;
      }
      continue;
    }
    if (live.next_send_us == kScheduleOnNextService) {
      live.next_send_us = now_us + kShareOfferRetryIntervalUs;
      continue;
    }
    if (now_us >= live.next_send_us) {
      if (TrySend(endpoint, live.offer->pending_packet)) {
        live.next_send_us = now_us + kShareOfferRetryIntervalUs;
      }
    }
  }
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
          DrivesInitialSnapshot(node.id(), share.share_id);
      bool const drive_catchup =
          phase != InitialSyncPhase::Complete && !drive_snapshot &&
          (phase == InitialSyncPhase::Pending ||
           ShareIntroducedBySharedEvent(*node, share.share_id));
      bool const drive_event = phase == InitialSyncPhase::Complete;
      if (!drive_snapshot && !drive_catchup && !drive_event) {
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
        if (phase != InitialSyncPhase::Complete &&
            DrivesInitialSnapshot(node.id(), share.share_id)) {
          SyncInitialState(node.id(), share.share_id);
        } else if (phase != InitialSyncPhase::Complete) {
          SyncCatchUp(node.id(), share.share_id);
        } else if (state->GetInitialSyncPhase() == InitialSyncPhase::Complete ||
                   phase == InitialSyncPhase::Complete) {
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
  ServiceRelays(now_us);
  ServiceOffers(now_us);
  ServiceShares(now_us);
}

void SharedSyncRuntime::OnShareOffer(std::string const& source_endpoint,
                                     ShareOfferFrame const& frame) {
  OnAdmission(source_endpoint, frame, ShareOfferKind::Grant);
}

void SharedSyncRuntime::OnShareRequest(std::string const& source_endpoint,
                                       ShareOfferFrame const& frame) {
  OnAdmission(source_endpoint, frame, ShareOfferKind::Request);
}

void SharedSyncRuntime::OnAdmission(std::string const& source_endpoint,
                                    ShareOfferFrame const& frame,
                                    ShareOfferKind kind) {
  if (source_endpoint.empty() ||
      source_endpoint == transport_.local_endpoint_uid()) {
    return;
  }
  if (auto existing = FindOfferByOperation(frame.operation_id)) {
    // Same id with a different source, node, kind, class, or requested
    // access is not a retry and does not replace the stored attempt.
    if (!AttemptMatches(*existing, source_endpoint, frame, kind)) {
      return;
    }
    if (existing->GetPhase() != ShareOfferPhase::AwaitingDecision &&
        !existing->pending_packet.empty()) {
      if (TrySend(source_endpoint, existing->pending_packet)) {
        NoteDirectSend(existing->operation_id);
      } else {
        MarkDecisionUnsent(existing->operation_id);
      }
    }
    return;
  }
  if (OpenAttemptBlocks(source_endpoint, frame.target_node_id)) {
    return;
  }

  bool protocol_reject = false;
  if (kind == ShareOfferKind::Request) {
    auto node = FindNode(frame.target_node_id);
    if (!node.is_valid() ||
        (frame.root_class_id != 0 &&
         frame.root_class_id != node->GetClassId())) {
      protocol_reject = true;
    }
  }

  auto offer = ShareOffer::ptr::Create(ae::CreateWith{domain_});
  InitializeRuntimeNode(*offer);
  auto event = OpenShareOfferEvent::ptr::Create(ae::CreateWith{domain_});
  event->role = static_cast<std::uint8_t>(ShareOfferRole::Responder);
  event->phase = static_cast<std::uint8_t>(ShareOfferPhase::AwaitingDecision);
  event->kind = static_cast<std::uint8_t>(kind);
  event->node_id = frame.target_node_id;
  event->root_class_id = frame.root_class_id;
  event->remote_endpoint = source_endpoint;
  event->access = frame.access;
  event->requested_access = frame.access;
  event->requested_class = frame.root_class_id;
  event->operation_id = frame.operation_id;
  offer->Commit(event);
  SaveOffer(offer);
  RememberOffer(offer, false);

  if (protocol_reject) {
    RejectJoin(frame.operation_id);
    return;
  }
  if (share_offer_notice_) {
    share_offer_notice_(ViewOf(*offer));
  }
  if (!share_offer_policy_) {
    return;
  }
  auto const granted = static_cast<ShareAccess>(frame.access);
  if (share_offer_policy_(ViewOf(*offer))) {
    AcceptJoin(frame.operation_id, granted, {});
  } else {
    RejectJoin(frame.operation_id);
  }
}

void SharedSyncRuntime::OnShareDecision(std::string const& source_endpoint,
                                        ShareDecisionFrame const& frame) {
  auto offer = FindOfferByOperation(frame.operation_id);
  if (!offer.is_valid() || offer->GetRole() != ShareOfferRole::Initiator) {
    return;
  }
  // Who answered is the transport source. A decision whose sender is not the
  // endpoint this offer named is ignored, even if the fields match.
  if (source_endpoint != offer->remote_endpoint) {
    return;
  }
  if (frame.target_node_id != offer->node_id) {
    return;
  }

  if (!frame.accepted) {
    if (offer->GetPhase() == ShareOfferPhase::Pending) {
      CommitOfferPhase(offer, ShareOfferPhase::Rejected, {});
      SaveOffer(offer);
    }
    return;
  }
  if (offer->GetPhase() == ShareOfferPhase::Rejected ||
      offer->GetPhase() == ShareOfferPhase::Complete ||
      offer->GetPhase() == ShareOfferPhase::Bound) {
    return;
  }

  if (offer->GetKind() == ShareOfferKind::Request) {
    if (frame.root_class_id == 0 || frame.access > 1) {
      return;
    }
    if (offer->GetPhase() == ShareOfferPhase::Pending) {
      CommitOfferPhase(offer, ShareOfferPhase::Admitted, {}, {}, frame.access,
                       frame.root_class_id);
      SaveOffer(offer);
    }
    return;
  }

  if (frame.root_class_id != offer->root_class_id ||
      frame.access != offer->requested_access) {
    return;
  }
  if (offer->GetPhase() != ShareOfferPhase::Pending &&
      offer->GetPhase() != ShareOfferPhase::Accepted) {
    return;
  }

  auto node = FindNode(offer->node_id);
  if (!node.is_valid()) {
    return;
  }
  if (!offer->remote_link.is_loaded()) {
    offer->remote_link.Load();
  }
  assert(offer->remote_link.is_loaded());

  if (offer->GetPhase() == ShareOfferPhase::Pending) {
    if (node->FindShareIndex(offer->remote_link.id()) >= node->shares.size()) {
      PublishAddShare(node, offer->remote_link, offer->GetAccess());
    }
    auto const share_index = node->FindShareIndex(offer->remote_link.id());
    assert(share_index < node->shares.size());
    if (node->shares[share_index].GetAccess() != offer->GetAccess()) {
      PublishShareAccess(node, node->shares[share_index].share_id, offer->GetAccess());
    }
    auto const share_id = node->shares[share_index].share_id;
    node.Save();
    for (auto& entry : node->link_sync_states) {
      entry.Save();
    }
    CommitOfferPhase(offer, ShareOfferPhase::Accepted, share_id);
    SaveOffer(offer);
    SyncInitialState(node.id(), share_id);
    if (!OutgoingOffline(offer->remote_endpoint)) {
      NoteDirectSync(node.id(), share_id);
    }
  }
}

SharedEventId SharedSyncRuntime::AllocateSharedIdentity() {
  auto const& origin = transport_.local_endpoint_uid();
  // Same origin as application events, so a relationship is attributable to
  // this endpoint and is not echoed back to it. The high half is reserved
  // for topology: an application counter that starts at 1 never reuses a
  // sequence this runtime already published.
  constexpr std::uint64_t kTopologySequenceFloor = std::uint64_t{1} << 63;
  std::uint64_t sequence = next_shared_sequence_ < kTopologySequenceFloor
                               ? kTopologySequenceFloor
                               : next_shared_sequence_;
  for (auto const& node : nodes_) {
    if (!node.is_valid() || !node.is_loaded()) {
      continue;
    }
    for (auto const& record : node->journal) {
      if (record.identity.origin_uid == origin &&
          record.identity.origin_sequence >= sequence) {
        sequence = record.identity.origin_sequence + 1;
      }
    }
  }
  if (sequence == 0) {
    sequence = 1;
  }
  next_shared_sequence_ = sequence + 1;
  return SharedEventId{.origin_uid = origin, .origin_sequence = sequence};
}

SharedEventOrder SharedSyncRuntime::AllocateSharedOrder(SharedNode const& node) {
  ++logical_now_us_;
  if (!node.journal.empty() &&
      logical_now_us_ <= node.journal.back().order.timestamp_us) {
    logical_now_us_ = node.journal.back().order.timestamp_us + 1;
  }
  return SharedEventOrder{.timestamp_us = logical_now_us_};
}

void SharedSyncRuntime::PublishAddShare(SharedNode::ptr node, Link::ptr link,
                                        ShareAccess access) {
  assert(node.is_valid() && node.is_loaded());
  assert(link.is_valid() && link.is_loaded());
  if (node->FindShareIndex(link.id()) < node->shares.size()) {
    return;
  }
  auto event = AddShareEvent::ptr::Create(ae::CreateWith{domain_});
  event->link = std::move(link);
  event->access = static_cast<std::uint8_t>(access);
  event->share_id = event.id();
  node->CommitShared(std::move(event), AllocateSharedIdentity(),
                     AllocateSharedOrder(*node));
  node.Save();
  for (auto& entry : node->link_sync_states) {
    entry.Save();
  }
}

void SharedSyncRuntime::PublishRemoveShare(SharedNode::ptr node,
                                           ae::ObjId share_id) {
  assert(node.is_valid() && node.is_loaded());
  assert(node->FindShareIndexForShare(share_id) < node->shares.size());
  auto event = RemoveShareEvent::ptr::Create(ae::CreateWith{domain_});
  event->share_id = share_id;
  node->CommitShared(std::move(event), AllocateSharedIdentity(),
                     AllocateSharedOrder(*node));
  node.Save();
  for (auto& entry : node->link_sync_states) {
    entry.Save();
  }
  RelayRemovedShare(node, share_id);
}

bool SharedSyncRuntime::FoldMissingSharedFromSnapshot(
    SharedNode::ptr node, std::string const& source_endpoint,
    NodeStateFrame const& frame, ShareOffer::ptr admission) {
  assert(node.is_valid() && node.is_loaded());
  assert(admission.is_valid());
  ae::RamDomainStorage parsed;
  if (!DeserializeObjectGraph(frame.payload, parsed)) {
    return false;
  }
  std::vector<StoredClassChainInfo> chains;
  if (!ValidateStoredClassChains(parsed, &chains)) {
    return false;
  }
  ae::Domain scratch_domain{parsed};
  ae::DomainGraph scratch_graph{&scratch_domain};
  auto candidate = scratch_graph.LoadRoot(frame.target_node_id);
  if (!candidate) {
    return false;
  }
  if (ae::Registry::GetRegistry().GenerationDistance(
          SharedNode::kClassId, candidate->GetClassId()) < 0) {
    return false;
  }
  auto& shared_candidate = static_cast<SharedNode&>(*candidate);
  if (!SnapshotIsAdmissible(shared_candidate, transport_.local_endpoint_uid(),
                            source_endpoint, frame.destination_share_id)) {
    return false;
  }
  auto const dest_index =
      shared_candidate.FindShareIndexForShare(frame.destination_share_id);
  if (dest_index >= shared_candidate.shares.size() ||
      shared_candidate.shares[dest_index].GetAccess() != admission->GetAccess()) {
    return false;
  }

  struct Missing {
    SharedEventId identity;
    std::uint64_t timestamp_us{0};
    std::uint32_t class_id{0};
    Event::ptr event;
  };
  std::vector<Missing> missing;
  for (auto const& record : shared_candidate.journal) {
    if (!record.HasSharedIdentity() || !record.event.is_valid()) {
      continue;
    }
    if (node->FindSharedEvent(record.identity) != nullptr) {
      continue;
    }
    auto event = record.event;
    if (!event.is_loaded()) {
      event.Load();
    }
    if (!event.is_loaded()) {
      return false;
    }
    missing.push_back(Missing{.identity = record.identity,
                              .timestamp_us = record.order.timestamp_us,
                              .class_id = event->GetClassId(),
                              .event = event});
  }
  std::sort(missing.begin(), missing.end(),
            [](Missing const& a, Missing const& b) {
              if (a.timestamp_us != b.timestamp_us) {
                return a.timestamp_us < b.timestamp_us;
              }
              return a.identity.origin_sequence < b.identity.origin_sequence;
            });

  for (auto const& item : missing) {
    std::vector<std::uint8_t> payload;
    if (!FreezeTopologyPayload(*item.event, payload)) {
      return false;
    }
    EventFrame const wire{
        .packet_id = frame.packet_id,
        .target_node_id = frame.target_node_id,
        .destination_share_id = frame.destination_share_id,
        .identity = item.identity,
        .timestamp_us = item.timestamp_us,
        .event_class_id = item.class_id,
        .payload = std::move(payload),
    };
    if (item.class_id == AddShareEvent::kClassId) {
      if (!ApplyIncomingAddShare(node, source_endpoint, wire)) {
        return false;
      }
      continue;
    }
    if (!IsTopologyEventClass(item.class_id) &&
        !IsStandaloneEventClassAllowed(item.class_id)) {
    return false;
    }
    ae::RamDomainStorage event_parsed;
    ae::ObjId wire_root_id;
    if (!ParseEventPayload(wire.payload, event_parsed, wire_root_id)) {
    return false;
    }
    if (!ValidateStandaloneEventGraph(event_parsed, wire_root_id,
                                      item.class_id)) {
    return false;
    }
    if (!PreflightHistoricalEventInsertion(*node, event_parsed, wire_root_id,
                                           item.class_id, item.identity,
                                           item.timestamp_us)) {
return false;
    }
    auto local_event = ImportStandaloneEventGraph(
        event_parsed, wire_root_id, item.class_id, domain_, storage_);
    if (!local_event) {
    return false;
    }
    auto local_event_ptr = Event::ptr::MakeFromThis(local_event.get());
    node->InsertShared(std::move(local_event_ptr), item.identity,
                       SharedEventOrder{.timestamp_us = item.timestamp_us});
    if (item.class_id == RemoveShareEvent::kClassId) {
      RelayRemovedShare(
          node, static_cast<RemoveShareEvent const&>(*local_event).share_id);
    }
  }

  if (node->FindShareIndexForShare(frame.destination_share_id) >=
      node->shares.size()) {
    return false;
  }
  auto const sync_index =
      node->FindLinkSyncIndexForShare(frame.destination_share_id);
  if (sync_index >= node->link_sync_states.size()) {
    return false;
  }
  auto state = node->link_sync_states[sync_index];
  if (!state.is_loaded()) {
    state.Load();
  }
  std::vector<SharedEventId> covered_ids;
  for (auto const& record : node->journal) {
    if (record.HasSharedIdentity() && !record.identity.origin_uid.empty()) {
      covered_ids.push_back(record.identity);
    }
  }
  if (state->GetInitialSyncPhase() != InitialSyncPhase::Complete) {
    state->CompleteFromReceivedSnapshot(std::move(covered_ids));
  }
  node.Save();
  state.Save();
  for (auto& entry : node->link_sync_states) {
    entry.Save();
  }
  return true;
}

void SharedSyncRuntime::RelayRemovedShare(SharedNode::ptr node,
                                          ae::ObjId share_id) {
  assert(node.is_valid() && node.is_loaded());
  if (node->FindShareIndexForShare(share_id) < node->shares.size()) {
    return;
  }
  auto const endpoint = EndpointIntroducedBy(*node, share_id);
  if (endpoint.empty() || endpoint == transport_.local_endpoint_uid()) {
    return;
  }
  auto const sync_index = node->FindLinkSyncIndexForShare(share_id);
  if (sync_index >= node->link_sync_states.size()) {
    return;
  }
  auto state = node->link_sync_states[sync_index];
  if (!state.is_loaded()) {
    state.Load();
  }
  auto const* record = LatestRemoveRecord(*node, share_id);
  if (record == nullptr || !record->event.is_valid() ||
      !record->event.is_loaded()) {
    return;
  }
  if (state->HasDelivered(record->identity)) {
    return;
  }
  if (state->HasPendingEvent()) {
    if (state->pending_event_identity == record->identity) {
      return;
    }
    // Abandon undelivered work to a relationship that is closing.
    state->CompleteIncrementalEvent();
    state.Save();
  }
  if (state->GetInitialSyncPhase() != InitialSyncPhase::Complete) {
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
      .target_node_id = node.id(),
      .destination_share_id = share_id,
      .identity = record->identity,
      .timestamp_us = record->order.timestamp_us,
      .event_class_id = RemoveShareEvent::kClassId,
      .payload = std::move(payload),
  };
  event->identity = record->identity;
  event->packet = EncodeEventFrame(frame);
  state->Commit(event);
  node.Save();
  state.Save();
  // Do not Send here. RegisterNode and load paths only arm; Service transmits.
  // Drop a prior open-share schedule so the first Service after close sends.
  for (auto& slot : sync_slots_) {
    if (slot.node_id == node.id() && slot.share_id == share_id) {
      slot.primed = false;
      slot.next_us = 0;
      return;
    }
  }
}

void SharedSyncRuntime::RelayAppliedRemovals(SharedNode::ptr node) {
  assert(node.is_valid() && node.is_loaded());
  for (auto const& record : node->journal) {
    if (!record.HasSharedIdentity() || !record.event.is_valid()) {
      continue;
    }
    auto event = record.event;
    if (!event.is_loaded()) {
      event.Load();
    }
    if (!event.is_loaded() ||
        event->GetClassId() != RemoveShareEvent::kClassId) {
      continue;
    }
    auto const removed =
        static_cast<RemoveShareEvent const&>(*event).share_id;
    if (!removed.is_valid() ||
        node->FindShareIndexForShare(removed) < node->shares.size()) {
      continue;
    }
    RelayRemovedShare(node, removed);
  }
}

void SharedSyncRuntime::PublishShareAccess(SharedNode::ptr node,
                                           ae::ObjId share_id,
                                           ShareAccess access) {
  assert(node.is_valid() && node.is_loaded());
  auto const index = node->FindShareIndexForShare(share_id);
  assert(index < node->shares.size());
  if (node->shares[index].GetAccess() == access) {
    return;
  }
  auto event = ChangeShareAccessEvent::ptr::Create(ae::CreateWith{domain_});
  event->share_id = share_id;
  event->access = static_cast<std::uint8_t>(access);
  node->CommitShared(std::move(event), AllocateSharedIdentity(),
                     AllocateSharedOrder(*node));
  node.Save();
}

void SharedSyncRuntime::ChangeShareAccess(ae::ObjId node_id,
                                          ae::ObjId share_id,
                                          ShareAccess access) {
  auto node = FindNode(node_id);
  assert(node.is_valid() && "ChangeShareAccess requires a registered node");
  PublishShareAccess(node, share_id, access);
}

void SharedSyncRuntime::RemoveShare(ae::ObjId node_id, ae::ObjId share_id) {
  auto node = FindNode(node_id);
  assert(node.is_valid() && "RemoveShare requires a registered node");
  PublishRemoveShare(node, share_id);
}

bool SharedSyncRuntime::IsTopologyEventClass(std::uint32_t class_id) const {
  return class_id == AddShareEvent::kClassId ||
         class_id == RemoveShareEvent::kClassId ||
         class_id == ChangeShareAccessEvent::kClassId;
}

bool SharedSyncRuntime::LocalShareAllowsWrite(SharedNode const& node) const {
  auto const* self =
      ShareOfEndpoint(node, transport_.local_endpoint_uid());
  return self != nullptr && self->GetAccess() == ShareAccess::ReadWrite;
}

void SharedSyncRuntime::SyncCatchUp(ae::ObjId node_id, ae::ObjId share_id) {
  auto node = FindNode(node_id);
  assert(node.is_valid());
  auto const* destination_endpoint = ShareEndpointOf(*node, share_id);
  assert(destination_endpoint != nullptr);
  auto const destination = *destination_endpoint;
  assert(destination != transport_.local_endpoint_uid());
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
      TrySend(destination, state->pending_initial_packet);
      return;
    case InitialSyncPhase::NotStarted:
      if (!ShareIntroducedBySharedEvent(*node, share_id)) {
        return;
      }
      break;
  }

  auto event = BeginInitialSyncEvent::ptr::Create(ae::CreateWith{domain_});
  ShareCatchUpFrame frame{
      .packet_id = event.id(),
      .target_node_id = node_id,
      .destination_share_id = share_id,
  };
  for (auto const& record : node->journal) {
    if (record.HasSharedIdentity()) {
      frame.have.push_back(record.identity);
    }
  }
  event->packet = EncodeShareCatchUpFrame(frame);
  // Empty on purpose. Completing this packet must not mark our own events
  // delivered; the peer's list is applied on their outgoing state.
  event->covered_event_ids.clear();
  state->Commit(event);
  node.Save();
  state.Save();
  TrySend(destination, state->pending_initial_packet);
}

void SharedSyncRuntime::OnCatchUp(std::string const& source_endpoint,
                                  ShareCatchUpFrame const& frame) {
  auto node = FindNode(frame.target_node_id);
  if (!node.is_valid()) {
    return;
  }
  auto const* destination =
      ShareEndpointOf(*node, frame.destination_share_id);
  if (destination == nullptr ||
      *destination != transport_.local_endpoint_uid()) {
    return;
  }
  auto const* source = ShareOfEndpoint(*node, source_endpoint);
  if (source == nullptr) {
    return;
  }
  auto const sync_index = node->FindLinkSyncIndexForShare(source->share_id);
  if (sync_index >= node->link_sync_states.size()) {
    return;
  }
  auto state = node->link_sync_states[sync_index];
  if (!state.is_loaded()) {
    state.Load();
  }
  state->NotePeerDelivered(frame.have);
  node.Save();
  state.Save();
  QueueAck(source_endpoint,
           AckFrame{
               .packet_id = frame.packet_id,
               .target_node_id = frame.target_node_id,
               .destination_share_id = frame.destination_share_id,
           });
}

bool SharedSyncRuntime::ApplyIncomingAddShare(
    SharedNode::ptr node, std::string const& source_endpoint,
    EventFrame const& frame) {
  (void)source_endpoint;
  ae::RamDomainStorage parsed;
  ae::ObjId wire_root_id;
  if (!ParseEventPayload(frame.payload, parsed, wire_root_id)) {
    return false;
  }
  std::vector<StoredClassChainInfo> chains;
  if (!ValidateStoredClassChains(parsed, &chains)) {
    return false;
  }

  ae::Domain scratch_domain{parsed};
  ae::DomainGraph scratch_graph{&scratch_domain};
  auto root = scratch_graph.LoadRoot(wire_root_id);
  if (!root || root->GetClassId() != AddShareEvent::kClassId) {
    return false;
  }
  auto& wire = static_cast<AddShareEvent&>(*root);
  if (!wire.share_id.is_valid() || !wire.link.is_valid()) {
    return false;
  }
  if (!wire.link.is_loaded()) {
    wire.link.Load();
  }
  if (!wire.link.is_loaded()) {
    return false;
  }
  if (ae::Registry::GetRegistry().GenerationDistance(
          Link::kClassId, wire.link->GetClassId()) < 0) {
    return false;
  }
  if (wire.link->EndpointUid().empty() || wire.access > 1) {
    return false;
  }
  if (node->FindShareIndexForShare(wire.share_id) < node->shares.size() ||
      node->FindShareIndex(wire.link.id()) < node->shares.size()) {
    return false;
  }

  auto& wire_link = static_cast<Node&>(*wire.link);
  if (ObjectOccupied(domain_, storage_, wire.link.id())) {
    auto live = FindOrLoad(domain_, storage_, wire.link.id());
    if (!live ||
        ae::Registry::GetRegistry().GenerationDistance(
            Node::kClassId, live->GetClassId()) < 0 ||
        !LinkDescriptorsMatch(static_cast<Node&>(*live), wire_link) ||
        !OccupiedClassMatches(domain_, storage_, parsed, chains,
                              wire.link.id())) {
      return false;
    }
  } else {
    for (Node* cursor = &wire_link; cursor->base.is_valid();) {
      if (ObjectOccupied(domain_, storage_, cursor->base.id())) {
        return false;
      }
      if (!cursor->base.is_loaded()) {
        cursor->base.Load();
      }
      cursor = &*cursor->base;
    }
    ClearLinkJournal(wire_link);
    wire.link.Save();
    for (Node* cursor = &wire_link; cursor->base.is_valid();) {
      cursor->base.Save();
      cursor = &*cursor->base;
    }
  }

  if (!PreflightAddShare(*node, wire, parsed, frame)) {
    return false;
  }

  Link::ptr local_link;
  if (ObjectOccupied(domain_, storage_, wire.link.id())) {
    local_link = AsLinkPtr(FindOrLoad(domain_, storage_, wire.link.id()));
  } else {
    auto const keep = LinkBaseChain(wire_link);
    ae::RamDomainStorage piece;
    for (auto const id : keep) {
      auto const it = parsed.state.find(id);
      if (it == parsed.state.end() || !it->second.has_value()) {
        return false;
      }
      piece.state.emplace(id, it->second);
    }
    CommitObjectGraph(piece, storage_);
    ae::DomainGraph graph{&domain_};
    auto loaded_link = graph.LoadRoot(wire.link.id());
    if (!loaded_link) {
      return false;
    }
    local_link = AsLinkPtr(loaded_link);
  }
  if (!local_link.is_valid()) {
    return false;
  }

  auto local = AddShareEvent::ptr::Create(ae::CreateWith{domain_});
  local->link = std::move(local_link);
  local->access = wire.access;
  local->share_id = wire.share_id;
  node->InsertShared(std::move(local), frame.identity,
                     SharedEventOrder{.timestamp_us = frame.timestamp_us});
  node.Save();
  for (auto& entry : node->link_sync_states) {
    entry.Save();
  }
  return true;
}

}  // namespace apptraverse
