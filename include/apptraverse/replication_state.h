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

struct OriginPackagingSnapshot {
  EventIdentity identity;
  std::vector<ae::ObjId> known_shared_before;

  AE_REFLECT_MEMBERS(identity, known_shared_before)
};

class ReplicationState : public ae::Obj {
  AE_OBJECT(ReplicationState, Obj, 0)

 protected:
  ReplicationState() = default;

 public:
  explicit ReplicationState(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(local_replica_id), AE_MMBR(known_peers),
                    AE_MMBR(outgoing), AE_MMBR(origin_packaging),
                    AE_MMBR(globally_confirmed), AE_MMBR(known_shared_ids),
                    AE_MMBR(lamport_clock), AE_MMBR(next_origin_sequence))

  ReplicaId local_replica_id;
  std::vector<ReplicaId> known_peers;
  std::vector<OutgoingDelivery> outgoing;
  std::vector<OriginPackagingSnapshot> origin_packaging;
  std::vector<EventIdentity> globally_confirmed;
  std::vector<ae::ObjId> known_shared_ids;
  std::uint64_t lamport_clock{0};
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

  bool IsGloballyConfirmed(EventIdentity const& identity) const {
    return std::find(globally_confirmed.begin(), globally_confirmed.end(),
                     identity) != globally_confirmed.end();
  }

  void MarkGloballyConfirmed(EventIdentity const& identity) {
    assert(identity.IsValid());
    if (!IsGloballyConfirmed(identity)) {
      globally_confirmed.push_back(identity);
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

  OriginPackagingSnapshot const* FindOriginPackaging(
      EventIdentity const& identity) const {
    for (auto const& snapshot : origin_packaging) {
      if (snapshot.identity == identity) {
        return &snapshot;
      }
    }
    return nullptr;
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

  std::uint64_t TickLamport(std::uint64_t received = 0) {
    lamport_clock = (received > lamport_clock ? received : lamport_clock) + 1;
    return lamport_clock;
  }
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_REPLICATION_STATE_H_
