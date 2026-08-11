#ifndef APPTRAVERSE_SHARED_GRAPH_SYNC_SESSION_H_
#define APPTRAVERSE_SHARED_GRAPH_SYNC_SESSION_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "aether/obj/obj_id.h"

#include "apptraverse/object_state.h"
#include "apptraverse/sync_packet.h"

namespace apptraverse {

// One session talks to exactly one remote peer. Peer identity is transport
// context only — never encoded into packets.
class SharedGraphSyncSession : public SyncPacketHandler {
 public:
  using SendFunction = std::function<void(SerializedSyncPacket)>;

  SharedGraphSyncSession(MemoryReplica local_replica, SendFunction send);

  void StartInitialSynchronization();
  void Poll();
  void RetryPending();
  void Receive(SerializedSyncPacket const& bytes);

  bool initial_sync_complete() const { return initial_sync_complete_; }
  std::size_t pending_packet_count() const { return pending_packets_.size(); }

 private:
  enum class PendingKind {
    kNodeState,
    kEvent,
    kNodeStateRequest,
  };

  struct PendingPacket {
    ae::ObjId packet_id;
    SerializedSyncPacket bytes;
    PendingKind kind{PendingKind::kNodeState};
    ae::ObjId node_id;
    ae::ObjId event_id;
    std::vector<ae::ObjId> event_ids;
    bool is_initial_state{false};
  };

  void Handle(NodeStatePacket const& packet) override;
  void Handle(EventPacket const& packet) override;
  void Handle(NodeStateRequestPacket const& packet) override;
  void Handle(AckPacket const& packet) override;

  void SendNodeState(Node::ptr node, bool is_initial);
  void SendEventPacket(Node::ptr node, EventRecord const& record);
  void EnsureNodeStateRequest(ae::ObjId requested_node_id);
  void SendAck(ae::ObjId acknowledged_packet_id);
  void MarkReceivedAndAck(ae::ObjId packet_id);
  void MaybeCompleteInitialSync();

  bool ContainsId(std::vector<ae::ObjId> const& ids, ae::ObjId id) const;
  void AddId(std::vector<ae::ObjId>& ids, ae::ObjId id);
  bool HasPendingNodeState(ae::ObjId node_id) const;
  bool HasPendingEvent(ae::ObjId event_id) const;
  bool HasPendingNodeStateRequest(ae::ObjId node_id) const;
  bool IsEventCoveredByPendingNodeState(ae::ObjId event_id) const;
  PendingPacket* FindPending(ae::ObjId packet_id);
  void ResendPendingNodeState(ae::ObjId node_id);

  bool StorageHasNode(ae::ObjId node_id) const;
  Node::ptr LoadLocalNode(ae::ObjId node_id);

  MemoryReplica local_;
  SendFunction send_;
  SyncPacketCodec codec_;
  ae::ObjId receiving_packet_id_;

  std::vector<ae::ObjId> known_node_ids_;
  std::vector<ae::ObjId> delivered_event_ids_;
  std::vector<ae::ObjId> successfully_received_packet_ids_;
  std::vector<PendingPacket> pending_packets_;

  bool initial_sync_started_{false};
  bool initial_sync_complete_{false};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_SHARED_GRAPH_SYNC_SESSION_H_
