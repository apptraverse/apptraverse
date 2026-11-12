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

// Identity within one author's stream for one SharedNode. Indices start at 1;
// timestamps strictly increase with that author's index, independently of
// arrival order. The journal still orders Events by timestamp only.
struct SharedEventStamp {
  std::string author_uid;
  std::uint64_t index{0};
  ae::ObjId event_id;
  std::uint64_t timestamp_us{0};

  AE_REFLECT_MEMBERS(author_uid, index, event_id, timestamp_us)
  bool is_valid() const;
  bool operator==(SharedEventStamp const&) const = default;
};

struct SharedStreamCursor {
  std::string author_uid;
  std::uint64_t index{0};
  std::uint64_t timestamp_us{0};

  AE_REFLECT_MEMBERS(author_uid, index, timestamp_us)
  bool operator==(SharedStreamCursor const&) const = default;
};

// A durable promise not to generate another Event at or before this time,
// together with the author's durable receipt evidence for the exact roster.
struct SharedProgressMarker {
  ae::ObjId marker_id;
  std::string author_uid;
  std::uint64_t through_timestamp_us{0};
  std::vector<std::string> members;
  std::vector<SharedStreamCursor> received;

  AE_REFLECT_MEMBERS(marker_id, author_uid, through_timestamp_us, members, received)
  bool is_valid() const;
  bool operator==(SharedProgressMarker const&) const = default;
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

// Exact S2 inline layout, before stream attribution was added.
struct SharedPendingPacketV2 {
  ae::ObjId packet_id;
  ae::ObjId event_id;
  bool initial{false};
  std::vector<ae::ObjId> covered_events;
  std::vector<std::uint8_t> bytes;
  bool coverage_notice{false};
  SharedAdmissionPlan admission;

  AE_REFLECT_MEMBERS(packet_id, event_id, initial, covered_events, bytes,
                    coverage_notice, admission)
};

// Exact S3 inline layout, before progress-control packets were introduced.
struct SharedPendingPacketV3 {
  ae::ObjId packet_id;
  ae::ObjId event_id;
  bool initial{false};
  std::vector<ae::ObjId> covered_events;
  std::vector<std::uint8_t> bytes;
  bool coverage_notice{false};
  SharedAdmissionPlan admission;
  SharedEventStamp stamp;

  AE_REFLECT_MEMBERS(packet_id, event_id, initial, covered_events, bytes,
                    coverage_notice, admission, stamp)
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
  SharedEventStamp stamp;
  bool progress{false};

  AE_REFLECT_MEMBERS(packet_id, event_id, initial, covered_events, bytes,
                    coverage_notice, admission, stamp, progress)

  bool operator==(SharedPendingPacket const&) const = default;
};

inline SharedPendingPacket UpgradeSharedPendingPacket(
    SharedPendingPacketV1 packet) {
  return {packet.packet_id, packet.event_id, packet.initial,
          std::move(packet.covered_events), std::move(packet.bytes), false, {}, {}};
}

inline SharedPendingPacket UpgradeSharedPendingPacket(
    SharedPendingPacketV2 packet) {
  return {packet.packet_id, packet.event_id, packet.initial,
          std::move(packet.covered_events), std::move(packet.bytes),
          packet.coverage_notice, std::move(packet.admission), {}};
}

inline SharedPendingPacket UpgradeSharedPendingPacket(
    SharedPendingPacketV3 packet) {
  return {packet.packet_id, packet.event_id, packet.initial,
          std::move(packet.covered_events), std::move(packet.bytes),
          packet.coverage_notice, std::move(packet.admission),
          std::move(packet.stamp), false};
}

class MarkSharedEventDeliveredEvent;
class QueueSharedPacketEvent;
class CompleteSharedPacketEvent;
class RememberSharedInitialEvent;
class InstallSharedAdmissionEvent;
class ConfirmSharedAdmissionEvent;
class ConfirmSharedStreamPrefixEvent;

// A local-persistent metadata Node for one endpoint of one SharedNode.
// Its own delivery Events never enter the shared journal. Recording delivery
// is local metadata only; transport and ACK processing are separate stages.
class SharedPeerState : public NodeFor<SharedPeerState> {
  APPTRAVERSE_OBJECT(SharedPeerState, Node, 4)

 protected:
  SharedPeerState() = default;

 public:
  explicit SharedPeerState(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(link), AE_MMBR(delivered_events),
                    AE_MMBR(pending_packets), AE_MMBR(initial_complete),
                    AE_MMBR(received_initial_packets), AE_MMBR(admission),
                    AE_MMBR(coverage_confirmed), AE_MMBR(delivered_notices),
                    AE_MMBR(acknowledged_streams))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, link, delivered_events);
    pending_packets.clear();
    initial_complete = false;
    received_initial_packets.clear();
    admission = {};
    coverage_confirmed = false;
    delivered_notices.clear();
    acknowledged_streams.clear();
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
    acknowledged_streams.clear();
  }

  template <typename Dnv>
  void Load(ae::Version<2>, Dnv& dnv) {
    std::vector<SharedPendingPacketV2> pending;
    dnv(base_, link, delivered_events, pending, initial_complete,
        received_initial_packets, admission, coverage_confirmed,
        delivered_notices);
    pending_packets.clear();
    pending_packets.reserve(pending.size());
    for (auto& packet : pending) {
      pending_packets.push_back(UpgradeSharedPendingPacket(std::move(packet)));
    }
    acknowledged_streams.clear();
  }

  template <typename Dnv>
  void Load(ae::Version<3>, Dnv& dnv) {
    std::vector<SharedPendingPacketV3> pending;
    dnv(base_, link, delivered_events, pending, initial_complete,
        received_initial_packets, admission, coverage_confirmed,
        delivered_notices, acknowledged_streams);
    pending_packets.clear();
    pending_packets.reserve(pending.size());
    for (auto& packet : pending) {
      pending_packets.push_back(UpgradeSharedPendingPacket(std::move(packet)));
    }
  }

  template <typename Dnv>
  void Load(ae::Version<4>, Dnv& dnv) {
    dnv(base_, link, delivered_events, pending_packets, initial_complete,
        received_initial_packets, admission, coverage_confirmed,
        delivered_notices, acknowledged_streams);
  }

  template <typename Dnv>
  void Save(ae::Version<4>, Dnv& dnv) const {
    dnv(base_, link, delivered_events, pending_packets, initial_complete,
        received_initial_packets, admission, coverage_confirmed,
        delivered_notices, acknowledged_streams);
  }

  SharedLinkDescriptor::ptr link;
  std::vector<ae::ObjId> delivered_events;
  std::vector<SharedPendingPacket> pending_packets;
  bool initial_complete{false};
  std::vector<ae::ObjId> received_initial_packets;
  SharedAdmissionPlan admission;
  bool coverage_confirmed{false};
  std::vector<ae::ObjId> delivered_notices;
  std::vector<SharedStreamCursor> acknowledged_streams;

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
  // Runtime validates the cursor against authored frames before recording it.
  // This local Event does not complete packets; completion remains explicit.
  void ConfirmPrefix(SharedStreamCursor cursor);
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
  bool CanApply(ConfirmSharedStreamPrefixEvent const& event) const;
  void Apply(ConfirmSharedStreamPrefixEvent const& event);

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
  APPTRAVERSE_OBJECT(QueueSharedPacketEvent, Event, 3)

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
  void Load(ae::Version<1>, Dnv& dnv) {
    SharedPendingPacketV2 old;
    dnv(base_, old);
    packet = UpgradeSharedPendingPacket(std::move(old));
  }
  template <typename Dnv>
  void Load(ae::Version<2>, Dnv& dnv) {
    SharedPendingPacketV3 old;
    dnv(base_, old);
    packet = UpgradeSharedPendingPacket(std::move(old));
  }
  template <typename Dnv>
  void Load(ae::Version<3>, Dnv& dnv) { dnv(base_, packet); }
  template <typename Dnv>
  void Save(ae::Version<3>, Dnv& dnv) const { dnv(base_, packet); }
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

class ConfirmSharedStreamPrefixEvent
    : public EventFor<SharedPeerState, ConfirmSharedStreamPrefixEvent> {
  APPTRAVERSE_OBJECT(ConfirmSharedStreamPrefixEvent, Event, 0)

 protected:
  ConfirmSharedStreamPrefixEvent() = default;

 public:
  explicit ConfirmSharedStreamPrefixEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(cursor))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) { dnv(base_, cursor); }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const { dnv(base_, cursor); }
  SharedStreamCursor cursor;
};

class AddSharedPeerStateEvent;
class NoteSharedLocalEventEvent;
class RememberSharedLocalFrameEvent;
class RememberSharedStreamEvent;
class RememberSharedProgressEvent;
class PruneSharedHistoryEvent;

// The entire collection is behind SharedNode::local_state, rather than a
// vector of individually local edges: even its size belongs to this replica.
class SharedNodeLocalState : public NodeFor<SharedNodeLocalState> {
  APPTRAVERSE_OBJECT(SharedNodeLocalState, Node, 4)

 protected:
  SharedNodeLocalState() = default;

 public:
  explicit SharedNodeLocalState(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(peers), AE_MMBR(local_events), AE_MMBR(local_frames),
                    AE_MMBR(stream_events), AE_MMBR(timestamp_floor),
                    AE_MMBR(progress_markers), AE_MMBR(checkpoint_us))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, peers);
    local_events.clear();
    local_frames.clear();
    stream_events.clear();
    timestamp_floor = 0;
    progress_markers.clear();
    checkpoint_us = 0;
  }

  template <typename Dnv>
  void Load(ae::Version<1>, Dnv& dnv) {
    dnv(base_, peers, local_events);
    local_frames.clear();
    stream_events.clear();
    timestamp_floor = 0;
    progress_markers.clear();
    checkpoint_us = 0;
  }

  template <typename Dnv>
  void Load(ae::Version<2>, Dnv& dnv) {
    std::vector<SharedPendingPacketV2> frames;
    dnv(base_, peers, local_events, frames);
    local_frames.clear();
    local_frames.reserve(frames.size());
    for (auto& frame : frames) {
      local_frames.push_back(UpgradeSharedPendingPacket(std::move(frame)));
    }
    stream_events.clear();
    timestamp_floor = 0;
    progress_markers.clear();
    checkpoint_us = 0;
  }

  template <typename Dnv>
  void Load(ae::Version<3>, Dnv& dnv) {
    std::vector<SharedPendingPacketV3> frames;
    dnv(base_, peers, local_events, frames, stream_events, timestamp_floor);
    local_frames.clear();
    local_frames.reserve(frames.size());
    for (auto& frame : frames) {
      local_frames.push_back(UpgradeSharedPendingPacket(std::move(frame)));
    }
    progress_markers.clear();
    checkpoint_us = 0;
  }

  template <typename Dnv>
  void Load(ae::Version<4>, Dnv& dnv) {
    dnv(base_, peers, local_events, local_frames, stream_events, timestamp_floor,
        progress_markers, checkpoint_us);
  }

  template <typename Dnv>
  void Save(ae::Version<4>, Dnv& dnv) const {
    dnv(base_, peers, local_events, local_frames, stream_events, timestamp_floor,
        progress_markers, checkpoint_us);
  }

  std::vector<SharedPeerState::ptr> peers;
  std::vector<ae::ObjId> local_events;
  std::vector<SharedPendingPacket> local_frames;
  std::vector<SharedEventStamp> stream_events;
  std::uint64_t timestamp_floor{0};
  std::vector<SharedProgressMarker> progress_markers;
  std::uint64_t checkpoint_us{0};

  void AddPeer(SharedPeerState::ptr peer);
  void NoteLocalEvent(ae::ObjId event_id);
  void RememberLocalFrame(SharedPendingPacket frame);
  SharedPendingPacket const* FindLocalFrame(ae::ObjId event_id) const;
  SharedEventStamp const* FindStreamEvent(ae::ObjId event_id) const;
  SharedEventStamp const* FindStreamIndex(std::string const& author_uid,
                                          std::uint64_t index) const;
  SharedStreamCursor ReceivedPrefix(std::string const& author_uid) const;
  // Accepts an identical known stamp; rejects ID/index reuse and timestamps
  // inconsistent with any earlier/later observed index from the same author.
  bool CanRememberStamp(SharedEventStamp const& stamp) const;
  void RememberStreamEvent(SharedEventStamp stamp);
  SharedProgressMarker const* FindProgress(std::string const& author_uid) const;
  void RememberProgress(SharedProgressMarker marker);
  void PruneHistory(std::uint64_t through_us);
  bool CanApply(AddSharedPeerStateEvent const& event) const;
  void Apply(AddSharedPeerStateEvent const& event);
  bool CanApply(NoteSharedLocalEventEvent const& event) const;
  void Apply(NoteSharedLocalEventEvent const& event);
  bool CanApply(RememberSharedLocalFrameEvent const& event) const;
  void Apply(RememberSharedLocalFrameEvent const& event);
  bool CanApply(RememberSharedStreamEvent const& event) const;
  void Apply(RememberSharedStreamEvent const& event);
  bool CanApply(RememberSharedProgressEvent const& event) const;
  void Apply(RememberSharedProgressEvent const& event);
  bool CanApply(PruneSharedHistoryEvent const& event) const;
  void Apply(PruneSharedHistoryEvent const& event);
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
  APPTRAVERSE_OBJECT(RememberSharedLocalFrameEvent, Event, 2)

 protected:
  RememberSharedLocalFrameEvent() = default;

 public:
  explicit RememberSharedLocalFrameEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(frame))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    SharedPendingPacketV2 old;
    dnv(base_, old);
    frame = UpgradeSharedPendingPacket(std::move(old));
  }
  template <typename Dnv>
  void Load(ae::Version<1>, Dnv& dnv) {
    SharedPendingPacketV3 old;
    dnv(base_, old);
    frame = UpgradeSharedPendingPacket(std::move(old));
  }
  template <typename Dnv>
  void Load(ae::Version<2>, Dnv& dnv) { dnv(base_, frame); }
  template <typename Dnv>
  void Save(ae::Version<2>, Dnv& dnv) const { dnv(base_, frame); }
  SharedPendingPacket frame;
};

class RememberSharedStreamEvent
    : public EventFor<SharedNodeLocalState, RememberSharedStreamEvent> {
  APPTRAVERSE_OBJECT(RememberSharedStreamEvent, Event, 0)

 protected:
  RememberSharedStreamEvent() = default;

 public:
  explicit RememberSharedStreamEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(stamp))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) { dnv(base_, stamp); }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const { dnv(base_, stamp); }
  SharedEventStamp stamp;
};

class RememberSharedProgressEvent
    : public EventFor<SharedNodeLocalState, RememberSharedProgressEvent> {
  APPTRAVERSE_OBJECT(RememberSharedProgressEvent, Event, 0)

 protected:
  RememberSharedProgressEvent() = default;

 public:
  explicit RememberSharedProgressEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(marker))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) { dnv(base_, marker); }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const { dnv(base_, marker); }
  SharedProgressMarker marker;
};

class PruneSharedHistoryEvent
    : public EventFor<SharedNodeLocalState, PruneSharedHistoryEvent> {
  APPTRAVERSE_OBJECT(PruneSharedHistoryEvent, Event, 0)

 protected:
  PruneSharedHistoryEvent() = default;

 public:
  explicit PruneSharedHistoryEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(through_us))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) { dnv(base_, through_us); }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const { dnv(base_, through_us); }
  std::uint64_t through_us{0};
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

  // Caller supplies the proof-checked closed time. Ordinary compaction remains
  // held; this operation folds only its eligible prefix and reports the actual
  // closed time if native stored-version guards refuse part of the request.
  std::uint64_t CompactConfirmedPrefix(std::uint64_t through_us);

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
