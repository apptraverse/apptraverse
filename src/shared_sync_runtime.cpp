#include "apptraverse/shared_sync_runtime.h"

#include <algorithm>
#include <cassert>
#include <utility>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/registry.h"

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
bool SnapshotIsAdmissible(SharedNode const& candidate,
                          std::string const& local_endpoint,
                          std::string const& source_endpoint,
                          ae::ObjId destination_share_id) {
  if (!candidate.base.is_valid() || !candidate.base.is_loaded()) {
    return false;
  }
  for (auto const& record : candidate.journal) {
    if (!record.event.is_valid() || !record.event.is_loaded() ||
        !record.event->CanApplyTo(candidate)) {
      return false;
    }
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
  NodeStateFrame const frame{
      .packet_id = event.id(),
      .target_node_id = node_id,
      .destination_share_id = share_id,
      .payload = SerializeNetworkSharedObjectGraph(*node),
  };
  event->packet = EncodeNodeStateFrame(frame);
  state->Commit(event);

  // Freeze and persist before the first send: bytes that were sent but not
  // persisted could not be retried unchanged after a restart.
  node.Save();
  state.Save();
  transport_.Send(destination, state->pending_initial_packet);
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
    if (!SnapshotIsAdmissible(static_cast<SharedNode const&>(*candidate),
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
  if (state->GetInitialSyncPhase() != InitialSyncPhase::Pending) {
    return;
  }
  if (state->pending_initial_packet_id != frame.packet_id) {
    return;
  }
  state->CompleteInitialSync();
  node.Save();
  state.Save();
}

}  // namespace apptraverse
