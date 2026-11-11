#ifndef APPTRAVERSE_SHARED_NODE_H_
#define APPTRAVERSE_SHARED_NODE_H_

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_link.h"
#include "apptraverse/object_macros.h"

namespace apptraverse {

// Shared identity/configuration of an endpoint. The local transport object
// may have a different concrete class on every replica. Native NetworkShared
// serialization writes its LocalPtr as empty and does not export that object.
// Node identity is retained by native historical replay. Set initial endpoint
// configuration before InitializeRuntimeNode captures the descriptor's base.
class SharedLinkDescriptor : public NodeFor<SharedLinkDescriptor> {
  APPTRAVERSE_OBJECT(SharedLinkDescriptor, Node, 0)

 protected:
  SharedLinkDescriptor() = default;

 public:
  explicit SharedLinkDescriptor(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(endpoint_uid), AE_MMBR(local_transport))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, endpoint_uid, local_transport);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, endpoint_uid, local_transport);
  }

  std::string endpoint_uid;
  LocalPtr<ae::Obj> local_transport;
};

// One immutable host admission. Coverage is assigned to the host until the
// joining peer's saved initial-state receipt confirms actual delivery.
struct SharedAdmissionPlan {
  ae::ObjId join_event_id;
  ae::ObjId snapshot_id;
  std::string host_endpoint;
  std::string joining_endpoint;
  std::vector<ae::ObjId> covered_events;

  AE_REFLECT_MEMBERS(join_event_id, snapshot_id, host_endpoint, joining_endpoint,
                    covered_events)

  bool is_valid() const;
  bool operator==(SharedAdmissionPlan const&) const = default;
};

// Exact S1 layout, used only by library schema loaders. Frozen message bytes
// are retained unchanged when older metadata is loaded.
struct SharedPendingPacketV1 {
  ae::ObjId packet_id;
  ae::ObjId event_id;
  bool initial{false};
  std::vector<ae::ObjId> covered_events;
  std::vector<std::uint8_t> bytes;

  AE_REFLECT_MEMBERS(packet_id, event_id, initial, covered_events, bytes)
};

// Frozen native message bytes and their delivery identity. This metadata is
// local to one SharedNode/peer relationship and never enters NetworkShared.
struct SharedPendingPacket {
  ae::ObjId packet_id;
  ae::ObjId event_id;
  bool initial{false};
  std::vector<ae::ObjId> covered_events;
  std::vector<std::uint8_t> bytes;
  bool coverage_notice{false};
  SharedAdmissionPlan admission;

  AE_REFLECT_MEMBERS(packet_id, event_id, initial, covered_events, bytes,
                    coverage_notice, admission)

  bool operator==(SharedPendingPacket const&) const = default;
};

inline SharedPendingPacket UpgradeSharedPendingPacket(
    SharedPendingPacketV1 packet) {
  return {packet.packet_id, packet.event_id, packet.initial,
          std::move(packet.covered_events), std::move(packet.bytes), false, {}};
}

class MarkSharedEventDeliveredEvent;
class QueueSharedPacketEvent;
class CompleteSharedPacketEvent;
class RememberSharedInitialEvent;
class InstallSharedAdmissionEvent;
class ConfirmSharedAdmissionEvent;

// A local-persistent metadata Node for one endpoint of one SharedNode.
// Its own delivery Events never enter the shared journal. Recording delivery
// is local metadata only; transport and ACK processing are separate stages.
class SharedPeerState : public NodeFor<SharedPeerState> {
  APPTRAVERSE_OBJECT(SharedPeerState, Node, 2)

 protected:
  SharedPeerState() = default;

 public:
  explicit SharedPeerState(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(link), AE_MMBR(delivered_events),
                    AE_MMBR(pending_packets), AE_MMBR(initial_complete),
                    AE_MMBR(received_initial_packets), AE_MMBR(admission),
                    AE_MMBR(coverage_confirmed), AE_MMBR(delivered_notices))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, link, delivered_events);
    pending_packets.clear();
    initial_complete = false;
    received_initial_packets.clear();
    admission = {};
    coverage_confirmed = false;
    delivered_notices.clear();
  }

  template <typename Dnv>
  void Load(ae::Version<1>, Dnv& dnv) {
    std::vector<SharedPendingPacketV1> pending;
    dnv(base_, link, delivered_events, pending, initial_complete,
        received_initial_packets);
    pending_packets.clear();
    pending_packets.reserve(pending.size());
    for (auto& packet : pending) {
      pending_packets.push_back(UpgradeSharedPendingPacket(std::move(packet)));
    }
    admission = {};
    coverage_confirmed = false;
    delivered_notices.clear();
  }

  template <typename Dnv>
  void Load(ae::Version<2>, Dnv& dnv) {
    dnv(base_, link, delivered_events, pending_packets, initial_complete,
        received_initial_packets, admission, coverage_confirmed,
        delivered_notices);
  }

  template <typename Dnv>
  void Save(ae::Version<2>, Dnv& dnv) const {
    dnv(base_, link, delivered_events, pending_packets, initial_complete,
        received_initial_packets, admission, coverage_confirmed,
        delivered_notices);
  }

  SharedLinkDescriptor::ptr link;
  std::vector<ae::ObjId> delivered_events;
  std::vector<SharedPendingPacket> pending_packets;
  bool initial_complete{false};
  std::vector<ae::ObjId> received_initial_packets;
  SharedAdmissionPlan admission;
  bool coverage_confirmed{false};
  std::vector<ae::ObjId> delivered_notices;

  void MarkDelivered(ae::ObjId event_id);
  bool HasDelivered(ae::ObjId event_id) const;
  void QueuePacket(SharedPendingPacket packet);
  void CompletePacket(ae::ObjId packet_id);
  void RememberInitial(ae::ObjId packet_id,
                       std::vector<ae::ObjId> covered_events);
  bool HasReceivedInitial(ae::ObjId packet_id) const;
  SharedPendingPacket const* FindPending(ae::ObjId packet_id) const;
  void InstallAdmission(SharedAdmissionPlan plan);
  void ConfirmAdmission(SharedAdmissionPlan plan);
  bool IsCoveredByAdmission(ae::ObjId event_id) const;
  bool HasNoticeDelivered(ae::ObjId packet_id) const;
  bool CanApply(MarkSharedEventDeliveredEvent const& event) const;
  void Apply(MarkSharedEventDeliveredEvent const& event);
  bool CanApply(QueueSharedPacketEvent const& event) const;
  void Apply(QueueSharedPacketEvent const& event);
  bool CanApply(CompleteSharedPacketEvent const& event) const;
  void Apply(CompleteSharedPacketEvent const& event);
  bool CanApply(RememberSharedInitialEvent const& event) const;
  void Apply(RememberSharedInitialEvent const& event);
  bool CanApply(InstallSharedAdmissionEvent const& event) const;
  void Apply(InstallSharedAdmissionEvent const& event);
  bool CanApply(ConfirmSharedAdmissionEvent const& event) const;
  void Apply(ConfirmSharedAdmissionEvent const& event);

 private:
  void AddDelivered(ae::ObjId event_id);
  void RemoveCoveredPending(std::vector<ae::ObjId> const& covered_events);
};

class MarkSharedEventDeliveredEvent
    : public EventFor<SharedPeerState, MarkSharedEventDeliveredEvent> {
  APPTRAVERSE_OBJECT(MarkSharedEventDeliveredEvent, Event, 0)

 protected:
  MarkSharedEventDeliveredEvent() = default;

 public:
  explicit MarkSharedEventDeliveredEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(event_id))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, event_id);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, event_id);
  }

  ae::ObjId event_id;
};

class QueueSharedPacketEvent
    : public EventFor<SharedPeerState, QueueSharedPacketEvent> {
  APPTRAVERSE_OBJECT(QueueSharedPacketEvent, Event, 1)

 protected:
  QueueSharedPacketEvent() = default;

 public:
  explicit QueueSharedPacketEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(packet))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    SharedPendingPacketV1 old;
    dnv(base_, old);
    packet = UpgradeSharedPendingPacket(std::move(old));
  }
  template <typename Dnv>
  void Load(ae::Version<1>, Dnv& dnv) { dnv(base_, packet); }
  template <typename Dnv>
  void Save(ae::Version<1>, Dnv& dnv) const { dnv(base_, packet); }
  SharedPendingPacket packet;
};

class CompleteSharedPacketEvent
    : public EventFor<SharedPeerState, CompleteSharedPacketEvent> {
  APPTRAVERSE_OBJECT(CompleteSharedPacketEvent, Event, 0)

 protected:
  CompleteSharedPacketEvent() = default;

 public:
  explicit CompleteSharedPacketEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(packet_id))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) { dnv(base_, packet_id); }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const { dnv(base_, packet_id); }
  ae::ObjId packet_id;
};

class RememberSharedInitialEvent
    : public EventFor<SharedPeerState, RememberSharedInitialEvent> {
  APPTRAVERSE_OBJECT(RememberSharedInitialEvent, Event, 0)

 protected:
  RememberSharedInitialEvent() = default;

 public:
  explicit RememberSharedInitialEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(packet_id), AE_MMBR(covered_events))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, packet_id, covered_events);
  }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, packet_id, covered_events);
  }
  ae::ObjId packet_id;
  std::vector<ae::ObjId> covered_events;
};

class InstallSharedAdmissionEvent
    : public EventFor<SharedPeerState, InstallSharedAdmissionEvent> {
  APPTRAVERSE_OBJECT(InstallSharedAdmissionEvent, Event, 0)

 protected:
  InstallSharedAdmissionEvent() = default;

 public:
  explicit InstallSharedAdmissionEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(plan))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) { dnv(base_, plan); }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const { dnv(base_, plan); }
  SharedAdmissionPlan plan;
};

class ConfirmSharedAdmissionEvent
    : public EventFor<SharedPeerState, ConfirmSharedAdmissionEvent> {
  APPTRAVERSE_OBJECT(ConfirmSharedAdmissionEvent, Event, 0)

 protected:
  ConfirmSharedAdmissionEvent() = default;

 public:
  explicit ConfirmSharedAdmissionEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(plan))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) { dnv(base_, plan); }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const { dnv(base_, plan); }
  SharedAdmissionPlan plan;
};

class AddSharedPeerStateEvent;
class NoteSharedLocalEventEvent;
class RememberSharedLocalFrameEvent;

// The entire collection is behind SharedNode::local_state, rather than a
// vector of individually local edges: even its size belongs to this replica.
class SharedNodeLocalState : public NodeFor<SharedNodeLocalState> {
  APPTRAVERSE_OBJECT(SharedNodeLocalState, Node, 2)

 protected:
  SharedNodeLocalState() = default;

 public:
  explicit SharedNodeLocalState(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(peers), AE_MMBR(local_events), AE_MMBR(local_frames))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, peers);
    local_events.clear();
    local_frames.clear();
  }

  template <typename Dnv>
  void Load(ae::Version<1>, Dnv& dnv) {
    dnv(base_, peers, local_events);
    local_frames.clear();
  }

  template <typename Dnv>
  void Load(ae::Version<2>, Dnv& dnv) {
    dnv(base_, peers, local_events, local_frames);
  }

  template <typename Dnv>
  void Save(ae::Version<2>, Dnv& dnv) const {
    dnv(base_, peers, local_events, local_frames);
  }

  std::vector<SharedPeerState::ptr> peers;
  std::vector<ae::ObjId> local_events;
  std::vector<SharedPendingPacket> local_frames;

  void AddPeer(SharedPeerState::ptr peer);
  void NoteLocalEvent(ae::ObjId event_id);
  void RememberLocalFrame(SharedPendingPacket frame);
  SharedPendingPacket const* FindLocalFrame(ae::ObjId event_id) const;
  bool CanApply(AddSharedPeerStateEvent const& event) const;
  void Apply(AddSharedPeerStateEvent const& event);
  bool CanApply(NoteSharedLocalEventEvent const& event) const;
  void Apply(NoteSharedLocalEventEvent const& event);
  bool CanApply(RememberSharedLocalFrameEvent const& event) const;
  void Apply(RememberSharedLocalFrameEvent const& event);
};

class NoteSharedLocalEventEvent
    : public EventFor<SharedNodeLocalState, NoteSharedLocalEventEvent> {
  APPTRAVERSE_OBJECT(NoteSharedLocalEventEvent, Event, 0)

 protected:
  NoteSharedLocalEventEvent() = default;

 public:
  explicit NoteSharedLocalEventEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(event_id))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) { dnv(base_, event_id); }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const { dnv(base_, event_id); }
  ae::ObjId event_id;
};

class RememberSharedLocalFrameEvent
    : public EventFor<SharedNodeLocalState, RememberSharedLocalFrameEvent> {
  APPTRAVERSE_OBJECT(RememberSharedLocalFrameEvent, Event, 0)

 protected:
  RememberSharedLocalFrameEvent() = default;

 public:
  explicit RememberSharedLocalFrameEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(frame))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) { dnv(base_, frame); }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const { dnv(base_, frame); }
  SharedPendingPacket frame;
};

class AddSharedPeerStateEvent
    : public EventFor<SharedNodeLocalState, AddSharedPeerStateEvent> {
  APPTRAVERSE_OBJECT(AddSharedPeerStateEvent, Event, 0)

 protected:
  AddSharedPeerStateEvent() = default;

 public:
  explicit AddSharedPeerStateEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(peer))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, peer);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, peer);
  }

  SharedPeerState::ptr peer;
};

class AddSharedLinkEvent;

// Persistence foundation only. Shared topology is event-sourced, while
// local_state belongs to this replica and survives historical shared replay.
// No peer-count limit or automatic compaction. Delivery is a separate runtime.
class SharedNode : public NodeFor<SharedNode> {
  APPTRAVERSE_OBJECT(SharedNode, Node, 0)

 protected:
  SharedNode() { SetJournalCompactionBlocked(true); }

 public:
  explicit SharedNode(ae::ObjProp prop) : NodeFor{prop} {
    SetJournalCompactionBlocked(true);
  }

  struct RebuildOperationScratch : Node::RebuildOperationScratch {
    LocalPtr<SharedNodeLocalState> local_state;
  };

  AE_OBJECT_REFLECT(AE_MMBR(links), AE_MMBR(local_state))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, links, local_state);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, links, local_state);
  }

  std::vector<SharedLinkDescriptor::ptr> links;

  // Attach this local root before InitializeRuntimeNode. Its AddPeer Events
  // mutate local membership independently of shared topology. Shared AddLink
  // replay does not allocate or repair local metadata objects.
  LocalPtr<SharedNodeLocalState> local_state;

  void AddLink(SharedLinkDescriptor::ptr link);
  bool CanApply(AddSharedLinkEvent const& event) const;
  void Apply(AddSharedLinkEvent const& event);

 protected:
  void StashLocalPersistentAcrossRebuild(void* rebuild_scratch) override;
  void RestoreLocalPersistentAcrossRebuild(void* rebuild_scratch) override;
};

class AddSharedLinkEvent : public EventFor<SharedNode, AddSharedLinkEvent> {
  APPTRAVERSE_OBJECT(AddSharedLinkEvent, Event, 0)

 protected:
  AddSharedLinkEvent() = default;

 public:
  explicit AddSharedLinkEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(link))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, link);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, link);
  }

  SharedLinkDescriptor::ptr link;
};

// Explicit opt-in keeps the static-library registration TU out of unrelated
// applications that do not use this foundation.
void EnsureSharedNodeRegistration();

}  // namespace apptraverse

#endif  // APPTRAVERSE_SHARED_NODE_H_
