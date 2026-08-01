#ifndef APPTRAVERSE_REPLICATION_STATE_H_
#define APPTRAVERSE_REPLICATION_STATE_H_

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <vector>

#include "aether/obj/obj.h"
#include "aether/obj/obj_id.h"

#include "apptraverse/event_identity.h"
#include "apptraverse/replica_id.h"

namespace apptraverse {

struct OutgoingDelivery {
  EventIdentity event_identity;
  ReplicaId recipient;
  bool acknowledged{false};

  AE_REFLECT_MEMBERS(event_identity, recipient, acknowledged)
};

struct OriginEventState {
  EventIdentity identity;
  ae::ObjId target_node;
  std::vector<ae::ObjId> known_shared_before;

  AE_REFLECT_MEMBERS(identity, target_node, known_shared_before)
};

class ReplicationState : public ae::Obj {
  AE_OBJECT(ReplicationState, Obj, 0)

 protected:
  ReplicationState() = default;

 public:
  explicit ReplicationState(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(local_replica_id), AE_MMBR(known_peers),
                    AE_MMBR(outgoing), AE_MMBR(origin_events),
                    AE_MMBR(known_shared_ids), AE_MMBR(known_shared_node_ids),
                    AE_MMBR(next_origin_sequence))

  ReplicaId local_replica_id;
  std::vector<ReplicaId> known_peers;
  std::vector<OutgoingDelivery> outgoing;
  std::vector<OriginEventState> origin_events;
  std::vector<ae::ObjId> known_shared_ids;
  std::vector<ae::ObjId> known_shared_node_ids;
  std::uint32_t next_origin_sequence{1};

  bool KnowsPeer(ReplicaId peer) const {
    return std::find(known_peers.begin(), known_peers.end(), peer) !=
           known_peers.end();
  }

  void AddPeer(ReplicaId peer) {
    assert(peer.IsValid());
    assert(peer != local_replica_id);
    if (!KnowsPeer(peer)) {
      known_peers.push_back(peer);
      std::sort(known_peers.begin(), known_peers.end());
    }
  }

  bool IsKnownShared(ae::ObjId id) const {
    return std::find(known_shared_ids.begin(), known_shared_ids.end(), id) !=
           known_shared_ids.end();
  }

  void RegisterShared(ae::ObjId id) {
    assert(id.IsValid());
    if (!IsKnownShared(id)) {
      known_shared_ids.push_back(id);
    }
  }

  bool IsKnownSharedNode(ae::ObjId id) const {
    return std::find(known_shared_node_ids.begin(), known_shared_node_ids.end(),
                     id) != known_shared_node_ids.end();
  }

  void RegisterSharedNode(ae::ObjId id) {
    assert(id.IsValid());
    RegisterShared(id);
    if (!IsKnownSharedNode(id)) {
      known_shared_node_ids.push_back(id);
    }
  }

  OutgoingDelivery* FindOutgoing(EventIdentity const& identity,
                                 ReplicaId recipient) {
    for (auto& delivery : outgoing) {
      if (delivery.event_identity == identity &&
          delivery.recipient == recipient) {
        return &delivery;
      }
    }
    return nullptr;
  }

  OriginEventState* FindOriginEvent(EventIdentity const& identity) {
    for (auto& entry : origin_events) {
      if (entry.identity == identity) {
        return &entry;
      }
    }
    return nullptr;
  }

  OriginEventState const* FindOriginEvent(EventIdentity const& identity) const {
    for (auto const& entry : origin_events) {
      if (entry.identity == identity) {
        return &entry;
      }
    }
    return nullptr;
  }

  bool HasOutgoing(EventIdentity const& identity) const {
    for (auto const& delivery : outgoing) {
      if (delivery.event_identity == identity) {
        return true;
      }
    }
    return false;
  }

  bool AllRecipientsAcknowledged(EventIdentity const& identity) const {
    bool found = false;
    for (auto const& delivery : outgoing) {
      if (delivery.event_identity != identity) {
        continue;
      }
      found = true;
      if (!delivery.acknowledged) {
        return false;
      }
    }
    return found;
  }

  std::uint32_t AllocateOriginSequence() {
    assert(next_origin_sequence != 0);
    assert(next_origin_sequence !=
           std::numeric_limits<std::uint32_t>::max());
    return next_origin_sequence++;
  }
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_REPLICATION_STATE_H_
