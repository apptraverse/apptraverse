#ifndef APPTRAVERSE_SHARED_NODE_H_
#define APPTRAVERSE_SHARED_NODE_H_

#include <cstdint>
#include <string>
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

// Frozen native message bytes and their delivery identity. This metadata is
// local to one SharedNode/peer relationship and never enters NetworkShared.
struct SharedPendingPacket {
  ae::ObjId packet_id;
  ae::ObjId event_id;
  bool initial{false};
  std::vector<ae::ObjId> covered_events;
  std::vector<std::uint8_t> bytes;

  AE_REFLECT_MEMBERS(packet_id, event_id, initial, covered_events, bytes)
};

class MarkSharedEventDeliveredEvent;
class QueueSharedPacketEvent;
class CompleteSharedPacketEvent;
class RememberSharedInitialEvent;

// A local-persistent metadata Node for one endpoint of one SharedNode.
// Its own delivery Events never enter the shared journal. Recording delivery
// is local metadata only; transport and ACK processing are separate stages.
class SharedPeerState : public NodeFor<SharedPeerState> {
  APPTRAVERSE_OBJECT(SharedPeerState, Node, 1)

 protected:
  SharedPeerState() = default;

 public:
  explicit SharedPeerState(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(link), AE_MMBR(delivered_events),
                    AE_MMBR(pending_packets), AE_MMBR(initial_complete),
                    AE_MMBR(received_initial_packets))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, link, delivered_events);
    pending_packets.clear();
    initial_complete = false;
    received_initial_packets.clear();
  }

  template <typename Dnv>
  void Load(ae::Version<1>, Dnv& dnv) {
    dnv(base_, link, delivered_events, pending_packets, initial_complete,
        received_initial_packets);
  }

  template <typename Dnv>
  void Save(ae::Version<1>, Dnv& dnv) const {
    dnv(base_, link, delivered_events, pending_packets, initial_complete,
        received_initial_packets);
  }

  SharedLinkDescriptor::ptr link;
  std::vector<ae::ObjId> delivered_events;
  std::vector<SharedPendingPacket> pending_packets;
  bool initial_complete{false};
  std::vector<ae::ObjId> received_initial_packets;

  void MarkDelivered(ae::ObjId event_id);
  bool HasDelivered(ae::ObjId event_id) const;
  void QueuePacket(SharedPendingPacket packet);
  void CompletePacket(ae::ObjId packet_id);
  void RememberInitial(ae::ObjId packet_id,
                       std::vector<ae::ObjId> covered_events);
  bool HasReceivedInitial(ae::ObjId packet_id) const;
  SharedPendingPacket const* FindPending(ae::ObjId packet_id) const;
  bool CanApply(MarkSharedEventDeliveredEvent const& event) const;
  void Apply(MarkSharedEventDeliveredEvent const& event);
  bool CanApply(QueueSharedPacketEvent const& event) const;
  void Apply(QueueSharedPacketEvent const& event);
  bool CanApply(CompleteSharedPacketEvent const& event) const;
  void Apply(CompleteSharedPacketEvent const& event);
  bool CanApply(RememberSharedInitialEvent const& event) const;
  void Apply(RememberSharedInitialEvent const& event);

 private:
  void AddDelivered(ae::ObjId event_id);
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
  APPTRAVERSE_OBJECT(QueueSharedPacketEvent, Event, 0)

 protected:
  QueueSharedPacketEvent() = default;

 public:
  explicit QueueSharedPacketEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(packet))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) { dnv(base_, packet); }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const { dnv(base_, packet); }
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

class AddSharedPeerStateEvent;
class NoteSharedLocalEventEvent;

// The entire collection is behind SharedNode::local_state, rather than a
// vector of individually local edges: even its size belongs to this replica.
class SharedNodeLocalState : public NodeFor<SharedNodeLocalState> {
  APPTRAVERSE_OBJECT(SharedNodeLocalState, Node, 1)

 protected:
  SharedNodeLocalState() = default;

 public:
  explicit SharedNodeLocalState(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(peers), AE_MMBR(local_events))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, peers);
    local_events.clear();
  }

  template <typename Dnv>
  void Load(ae::Version<1>, Dnv& dnv) {
    dnv(base_, peers, local_events);
  }

  template <typename Dnv>
  void Save(ae::Version<1>, Dnv& dnv) const {
    dnv(base_, peers, local_events);
  }

  std::vector<SharedPeerState::ptr> peers;
  std::vector<ae::ObjId> local_events;

  void AddPeer(SharedPeerState::ptr peer);
  void NoteLocalEvent(ae::ObjId event_id);
  bool CanApply(AddSharedPeerStateEvent const& event) const;
  void Apply(AddSharedPeerStateEvent const& event);
  bool CanApply(NoteSharedLocalEventEvent const& event) const;
  void Apply(NoteSharedLocalEventEvent const& event);
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
// No peer-count limit, wire protocol, networking or automatic compaction.
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
