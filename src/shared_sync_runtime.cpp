#include "apptraverse/shared_sync_runtime.h"

#include <algorithm>
#include <cassert>
#include <utility>

#include "apptraverse/shared_network_graph.h"

namespace apptraverse {
namespace {

// The Link of a Share, loaded. Share topology is shared state, so both
// replicas see the same descriptors and resolve endpoints the same way.
Link::ptr const& ShareLink(Share const& share) {
  if (!share.link.is_loaded()) {
    share.link.Load();
  }
  return share.link;
}

bool TopologyKnowsEndpoint(SharedNode const& node,
                           std::string const& endpoint_uid) {
  for (auto const& share : node.shares) {
    if (ShareLink(share)->EndpointUid() == endpoint_uid) {
      return true;
    }
  }
  return false;
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

  auto const share_index = node->FindShareIndexForShare(share_id);
  assert(share_index < node->shares.size() &&
         "SyncInitialState requires an open Share relationship");
  auto const& destination = ShareLink(node->shares[share_index])->EndpointUid();
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
        OnAck(frame);
      }
      break;
    }
  }
}

void SharedSyncRuntime::OnNodeState(std::string const& source_endpoint,
                                    NodeStateFrame const& frame) {
  auto node = FindNode(frame.target_node_id);
  if (!node.is_valid()) {
    // Untrusted bytes may not create arbitrary roots: only a SharedNode this
    // replica is waiting for.
    if (!IsExpectedInitialNode(frame.target_node_id)) {
      return;
    }
    if (!ImportObjectGraphPayload(frame.payload, storage_)) {
      return;
    }
    node = SharedNode::ptr::Declare(
        ae::CreateWith{domain_}.with_id(frame.target_node_id));
    node.Load();
    if (!node.is_loaded()) {
      return;
    }
    // Sender-local sync state is excluded from the snapshot by design.
    // Replaying the imported shared journal builds this replica's own
    // LinkSyncState for every Share, keyed by the relationship identity that
    // travelled with the topology.
    node->ReplayFromBase();
    nodes_.push_back(node);
  }

  auto const share_index = node->FindShareIndexForShare(
      frame.destination_share_id);
  if (share_index >= node->shares.size()) {
    return;
  }
  // This replica must be the destination of the relationship, and the sender
  // must be an endpoint the shared topology knows.
  if (ShareLink(node->shares[share_index])->EndpointUid() !=
      transport_.local_endpoint_uid()) {
    return;
  }
  if (!TopologyKnowsEndpoint(*node, source_endpoint)) {
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

  if (state->received_initial_packet_id != frame.packet_id) {
    if (state->received_initial_packet_id.is_valid()) {
      // A second, different initial snapshot is not part of protocol v1.
      return;
    }
    state->NoteInitialSyncReceived(frame.packet_id);
    node.Save();
    state.Save();
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

void SharedSyncRuntime::OnAck(AckFrame const& frame) {
  auto node = FindNode(frame.target_node_id);
  if (!node.is_valid()) {
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
