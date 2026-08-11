#ifndef APPTRAVERSE_SYNC_SESSION_STATE_H_
#define APPTRAVERSE_SYNC_SESSION_STATE_H_

#include <cstdint>
#include <vector>

#include "aether-miscpp/reflect/reflect.h"
#include "aether/obj/domain.h"
#include "aether/obj/obj_id.h"

#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"

namespace apptraverse {

enum class PendingSyncPacketKind : std::uint8_t {
  kNodeState = 0,
  kEvent = 1,
};

struct PendingSyncPacketState {
  ae::ObjId packet_id;
  std::vector<std::uint8_t> serialized_bytes;
  PendingSyncPacketKind kind{PendingSyncPacketKind::kNodeState};
  ae::ObjId node_id;
  ae::ObjId event_id;
  std::vector<ae::ObjId> event_ids;
  bool is_initial_state{false};

  AE_REFLECT_MEMBERS(packet_id, serialized_bytes, kind, node_id, event_id,
                     event_ids, is_initial_state)
};

struct SyncSessionData {
  ae::ObjId shared_root_id;
  std::vector<ae::ObjId> delivered_event_ids;
  std::vector<ae::ObjId> successfully_received_packet_ids;
  std::vector<PendingSyncPacketState> pending_packets;
  bool initial_sync_started{false};
  bool initial_sync_complete{false};

  AE_REFLECT_MEMBERS(shared_root_id, delivered_event_ids,
                     successfully_received_packet_ids, pending_packets,
                     initial_sync_started, initial_sync_complete)
};

class SyncSessionState;
class SetSyncSessionDataEvent;

class SetSyncSessionDataEvent
    : public EventFor<SyncSessionState, SetSyncSessionDataEvent> {
  APPTRAVERSE_OBJECT(SetSyncSessionDataEvent, Event, 0)

 protected:
  SetSyncSessionDataEvent() = default;

 public:
  explicit SetSyncSessionDataEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(data))

  SyncSessionData data;
};

// Persistent state for exactly one remote-peer synchronization session.
// No Aether UID, peer name, transport, or peer list.
class SyncSessionState : public NodeFor<SyncSessionState> {
  APPTRAVERSE_OBJECT(SyncSessionState, Node, 0)

 protected:
  SyncSessionState() = default;

 public:
  explicit SyncSessionState(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(data))

  SyncSessionData data;

  void Apply(SetSyncSessionDataEvent const& event) { data = event.data; }
};

SyncSessionState::ptr CreateSyncSessionState(ae::Domain& domain,
                                             ae::ObjId shared_root_id);

}  // namespace apptraverse

#endif  // APPTRAVERSE_SYNC_SESSION_STATE_H_
