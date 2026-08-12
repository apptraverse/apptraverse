#ifndef APPTRAVERSE_SHARED_GRAPH_SYNC_SESSION_H_
#define APPTRAVERSE_SHARED_GRAPH_SYNC_SESSION_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "aether/obj/obj_id.h"

#include "apptraverse/object_graph_copy.h"
#include "apptraverse/sync_packet.h"
#include "apptraverse/sync_session_state.h"

namespace apptraverse {

// One session talks to exactly one remote peer. Peer identity is transport
// context only — never encoded into packets. Durable progress lives in
// SyncSessionState.
class SharedGraphSyncSession : public SyncPacketHandler {
 public:
  // packet_id is runtime tracing context only (also ObjId of the packet root).
  using SendFunction =
      std::function<void(ae::ObjId packet_id, SerializedSyncPacket)>;
  using TraceFunction = std::function<void(std::string const& line)>;

  SharedGraphSyncSession(SyncReplica local_replica,
                         SyncSessionState::ptr state, SendFunction send);

  void set_trace(TraceFunction trace) { trace_ = std::move(trace); }

  void StartOrResume();
  void Poll();
  void RetryPending();
  void Receive(SerializedSyncPacket const& bytes);

  bool initial_sync_complete() const {
    return state_->data.initial_sync_complete;
  }
  std::size_t pending_packet_count() const {
    return state_->data.pending_packets.size();
  }

  // Runtime-only. Increases when Handle(AckPacket) removes a real pending
  // packet. Unknown/duplicate ACKs do not change it. Never serialized.
  std::uint64_t ack_progress_revision() const { return ack_progress_revision_; }

  SyncSessionState::ptr state() const { return state_; }

 private:
  void Handle(NodeStatePacket const& packet) override;
  void Handle(EventPacket const& packet) override;
  void Handle(AckPacket const& packet) override;

  void CommitData(SyncSessionData data);
  void SendInitialNodeState(Node::ptr root);
  void SendEventPacket(Node::ptr node, EventRecord const& record);
  void SendAck(ae::ObjId acknowledged_packet_id);
  void MarkReceivedAndAck(ae::ObjId packet_id);

  bool ContainsId(std::vector<ae::ObjId> const& ids, ae::ObjId id) const;
  void AddId(std::vector<ae::ObjId>& ids, ae::ObjId id);
  bool HasPendingEvent(ae::ObjId event_id) const;
  bool HasPendingInitialNodeState() const;
  bool IsEventCoveredByPendingNodeState(ae::ObjId event_id) const;
  PendingSyncPacketState* FindPending(SyncSessionData& data,
                                      ae::ObjId packet_id);

  bool StorageHasNode(ae::ObjId node_id) const;
  Node::ptr LoadLocalNode(ae::ObjId node_id);

  std::vector<ae::ObjId> CollectSharedGraphEventIds(Node::ptr root);

  void MergeNodeStateGraph(NodeStatePacket const& packet,
                           ae::IDomainStorage& decoded_storage);
  Event::ptr ImportEventForAccept(Event::ptr decoded_event,
                                  ae::IDomainStorage& decoded_storage);

  SyncReplica local_;
  SyncSessionState::ptr state_;
  SendFunction send_;
  TraceFunction trace_;
  ae::ObjId receiving_packet_id_;
  DecodedSyncPacket* receiving_decoded_{nullptr};
  std::uint64_t ack_progress_revision_{0};

  void Trace(std::string const& line) const;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_SHARED_GRAPH_SYNC_SESSION_H_
