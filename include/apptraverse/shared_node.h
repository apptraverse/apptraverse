#ifndef APPTRAVERSE_SHARED_NODE_H_
#define APPTRAVERSE_SHARED_NODE_H_

#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "aether-miscpp/reflect/reflect.h"

#include "apptraverse/event_for.h"
#include "apptraverse/link.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_link.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/shared_event_id.h"

namespace apptraverse {

// Permanent-pair access: wire layout keeps uint8; only ReadWrite (=0) is
// valid. Non-zero access is refused at InstallLocalShare / CanApply.
enum class ShareAccess : std::uint8_t {
  ReadWrite = 0,
};

// One lifetime of SharedNode <-> Link. share_id is the relationship identity
// carried on AddShareEvent. A local commit uses that event's ObjId. A network
// import keeps the same id even when the receiver allocates a new event
// object. Permanent AeroAdmin dialogs hold at most two shares.
struct Share {
  ae::ObjId share_id;
  Link::ptr link;
  std::uint8_t access{
      static_cast<std::uint8_t>(ShareAccess::ReadWrite)};

  AE_REFLECT_MEMBERS(share_id, link, access)

  ShareAccess GetAccess() const {
    return static_cast<ShareAccess>(access);
  }

  void SetAccess(ShareAccess value) {
    access = static_cast<std::uint8_t>(value);
  }
};

enum class InitialSyncPhase : std::uint8_t {
  NotStarted = 0,
  Pending = 1,
  Complete = 2,
};

class SetLinkInitialSyncPhaseEvent;
class BeginInitialSyncEvent;
class CompleteInitialSyncEvent;
class NoteInitialSyncReceivedEvent;
class CompleteFromReceivedSnapshotEvent;
class BeginIncrementalEventSyncEvent;
class CompleteIncrementalEventSyncEvent;

// Local-persistent synchronization progress for one Share relationship of one
// SharedNode. Belongs to share_id, not to the Link: a later relationship over
// the same Link gets its own state.
// Event-sourced Node: phase changes go through Commit/Apply. Reachable from
// SharedNode only via LocalPtr, so network shared-graph serialization excludes
// it without SharedNode-specific sanitization.
class LinkSyncState : public NodeFor<LinkSyncState> {
  APPTRAVERSE_OBJECT(LinkSyncState, Node, 3)

 protected:
  LinkSyncState() = default;

 public:
  explicit LinkSyncState(ae::ObjProp prop) : NodeFor{prop} {}

  // Persist via explicit Load/Save (v3). Reflection lists only graph edges
  // DomainVisitor must follow; MSVC cannot nest mirrors for all wire fields.
  AE_OBJECT_REFLECT(AE_MMBR(share_id), AE_MMBR(link))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv&) {
    throw std::runtime_error("LinkSyncState v0 is not supported");
  }

  template <typename Dnv>
  void Load(ae::Version<1>, Dnv&) {
    throw std::runtime_error(
        "LinkSyncState v1 predates incremental Event delivery; "
        "re-distill with a fresh state dir");
  }

  template <typename Dnv>
  void Load(ae::Version<2>, Dnv&) {
    throw std::runtime_error(
        "LinkSyncState v2 flattened layout is not supported; "
        "re-distill with a fresh state dir");
  }

  template <typename Dnv>
  void Load(ae::Version<3>, Dnv& dnv) {
    dnv(base_, share_id, link, initial_sync_phase, pending_initial_packet_id,
        pending_initial_packet, received_initial_packet_id,
        pending_initial_covered_event_ids, delivered_event_ids,
        pending_event_packet_id, pending_event_identity, pending_event_packet);
  }

  template <typename Dnv>
  void Save(ae::Version<3>, Dnv& dnv) const {
    dnv(base_, share_id, link, initial_sync_phase, pending_initial_packet_id,
        pending_initial_packet, received_initial_packet_id,
        pending_initial_covered_event_ids, delivered_event_ids,
        pending_event_packet_id, pending_event_identity, pending_event_packet);
  }

  ae::ObjId share_id;
  Link::ptr link;
  std::uint8_t initial_sync_phase{
      static_cast<std::uint8_t>(InitialSyncPhase::NotStarted)};

  // Sender side: the exact frozen initial-state frame, resent byte for byte
  // until it is acknowledged. Retries never rebuild it from current state.
  ae::ObjId pending_initial_packet_id;
  std::vector<std::uint8_t> pending_initial_packet;

  // Receiver side: the initial-state packet already imported and persisted.
  // A repeat of it is a duplicate to acknowledge again, not state to re-apply.
  ae::ObjId received_initial_packet_id;

  // SharedEventIds inside the frozen initial snapshot. Moved into
  // delivered_event_ids when the initial ACK completes, so they are not
  // resent as incremental Events. Events committed after the freeze are not
  // listed here.
  std::vector<SharedEventId> pending_initial_covered_event_ids;

  // Incremental delivery: identities already acknowledged for this Share,
  // plus at most one outstanding frozen Event packet.
  std::vector<SharedEventId> delivered_event_ids;
  ae::ObjId pending_event_packet_id;
  SharedEventId pending_event_identity;
  std::vector<std::uint8_t> pending_event_packet;

  InitialSyncPhase GetInitialSyncPhase() const {
    return static_cast<InitialSyncPhase>(initial_sync_phase);
  }

  void SetInitialSyncPhase(InitialSyncPhase phase);
  void CompleteInitialSync();
  void NoteInitialSyncReceived(ae::ObjId packet_id);
  void CompleteFromReceivedSnapshot(std::vector<SharedEventId> delivered);
  // Confirmed delivery: pending identity enters delivered_event_ids.
  void CompleteIncrementalEvent();

  bool HasDelivered(SharedEventId const& identity) const;
  bool HasPendingEvent() const {
    return pending_event_packet_id.is_valid();
  }
  bool HasPendingInitial() const {
    return pending_initial_packet_id.is_valid();
  }

  void Apply(SetLinkInitialSyncPhaseEvent const& event);
  void Apply(BeginInitialSyncEvent const& event);
  void Apply(CompleteInitialSyncEvent const& event);
  void Apply(NoteInitialSyncReceivedEvent const& event);
  bool CanApply(CompleteFromReceivedSnapshotEvent const& event) const;
  void Apply(CompleteFromReceivedSnapshotEvent const& event);
  bool CanApply(BeginIncrementalEventSyncEvent const& event) const;
  void Apply(BeginIncrementalEventSyncEvent const& event);
  void Apply(CompleteIncrementalEventSyncEvent const& event);
};

class SetLinkInitialSyncPhaseEvent
    : public EventFor<LinkSyncState, SetLinkInitialSyncPhaseEvent> {
  APPTRAVERSE_OBJECT(SetLinkInitialSyncPhaseEvent, Event, 0)

 protected:
  SetLinkInitialSyncPhaseEvent() = default;

 public:
  explicit SetLinkInitialSyncPhaseEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(phase))

  std::uint8_t phase{
      static_cast<std::uint8_t>(InitialSyncPhase::NotStarted)};
};

// Freeze one initial-state packet for this relationship. The packet identity
// is this Event's ObjId, so it is stable across retry and restart and can be
// embedded in the frozen frame before the Event is committed.
class BeginInitialSyncEvent
    : public EventFor<LinkSyncState, BeginInitialSyncEvent> {
  APPTRAVERSE_OBJECT(BeginInitialSyncEvent, Event, 0)

 protected:
  BeginInitialSyncEvent() = default;

 public:
  explicit BeginInitialSyncEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(packet), AE_MMBR(covered_event_ids))

  std::vector<std::uint8_t> packet;
  std::vector<SharedEventId> covered_event_ids;
};

// Sender: the frozen packet was acknowledged by the peer.
class CompleteInitialSyncEvent
    : public EventFor<LinkSyncState, CompleteInitialSyncEvent> {
  APPTRAVERSE_OBJECT(CompleteInitialSyncEvent, Event, 0)

 protected:
  CompleteInitialSyncEvent() = default;

 public:
  explicit CompleteInitialSyncEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT()
};

// Receiver: an initial-state packet was imported into this replica.
class NoteInitialSyncReceivedEvent
    : public EventFor<LinkSyncState, NoteInitialSyncReceivedEvent> {
  APPTRAVERSE_OBJECT(NoteInitialSyncReceivedEvent, Event, 0)

 protected:
  NoteInitialSyncReceivedEvent() = default;

 public:
  explicit NoteInitialSyncReceivedEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(packet_id))

  ae::ObjId packet_id;
};

// Receiver: marks the source Share Complete upon importing an initial snapshot,
// recording covered events already known to the sender.
class CompleteFromReceivedSnapshotEvent
    : public EventFor<LinkSyncState, CompleteFromReceivedSnapshotEvent> {
  APPTRAVERSE_OBJECT(CompleteFromReceivedSnapshotEvent, Event, 0)

 protected:
  CompleteFromReceivedSnapshotEvent() = default;

 public:
  explicit CompleteFromReceivedSnapshotEvent(ae::ObjProp prop)
      : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(delivered_event_ids))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, delivered_event_ids);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, delivered_event_ids);
  }

  std::vector<SharedEventId> delivered_event_ids;
};

// Freeze one incremental standalone Event packet for this relationship.
// Packet identity is this Event's ObjId. Requires InitialSyncPhase::Complete.
class BeginIncrementalEventSyncEvent
    : public EventFor<LinkSyncState, BeginIncrementalEventSyncEvent> {
  APPTRAVERSE_OBJECT(BeginIncrementalEventSyncEvent, Event, 0)

 protected:
  BeginIncrementalEventSyncEvent() = default;

 public:
  explicit BeginIncrementalEventSyncEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(identity), AE_MMBR(packet))

  SharedEventId identity;
  std::vector<std::uint8_t> packet;
};

// Sender: the frozen incremental Event packet was acknowledged.
class CompleteIncrementalEventSyncEvent
    : public EventFor<LinkSyncState, CompleteIncrementalEventSyncEvent> {
  APPTRAVERSE_OBJECT(CompleteIncrementalEventSyncEvent, Event, 0)

 protected:
  CompleteIncrementalEventSyncEvent() = default;

 public:
  explicit CompleteIncrementalEventSyncEvent(ae::ObjProp prop)
      : EventFor{prop} {}

  AE_OBJECT_REFLECT()
};

class AddShareEvent;

// Generic shared Node: shared topology (shares[]) plus local-persistent
// per-Link sync metadata (link_sync_states via LocalPtr). Permanent pair:
// at most two ReadWrite shares; no Remove/ChangeAccess events.
class SharedNode : public NodeFor<SharedNode> {
  APPTRAVERSE_OBJECT(SharedNode, Node, 2)

 protected:
  SharedNode() = default;

 public:
  explicit SharedNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(shares), AE_MMBR(link_sync_states))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv&) {
    throw std::runtime_error("SharedNode v0 is not supported");
  }

  template <typename Dnv>
  void Load(ae::Version<1>, Dnv&) {
    throw std::runtime_error(
        "SharedNode v1 flattened layout is not supported; re-distill with a "
        "fresh state dir");
  }

  template <typename Dnv>
  void Load(ae::Version<2>, Dnv& dnv) {
    dnv(base_, shares, link_sync_states);
  }

  template <typename Dnv>
  void Save(ae::Version<2>, Dnv& dnv) const {
    dnv(base_, shares, link_sync_states);
  }

  std::vector<Share> shares;
  std::vector<LocalPtr<LinkSyncState>> link_sync_states;

  // Local-only topology for chat formation (self + peer). Refuses a third
  // share and any access other than ReadWrite.
  void InstallLocalShare(Link::ptr link, ShareAccess access);

  bool CanApply(AddShareEvent const& event) const;
  void Apply(AddShareEvent const& event);

  // Local sync phase changes are Events on the LinkSyncState Node of the
  // active Share relationship (created by AddShare Apply). Not shared Events.
  void SetInitialSyncPhase(Link::ptr link, InitialSyncPhase phase);
  InitialSyncPhase GetInitialSyncPhase(Link::ptr link) const;

  void StashLocalPersistentAcrossRebuild() override;
  void RestoreLocalPersistentAcrossRebuild() override;

  // Active relationship for a Link. A Link has at most one at a time.
  std::size_t FindShareIndex(ae::ObjId link_id) const;
  std::size_t FindShareIndexForShare(ae::ObjId share_id) const;
  std::size_t FindLinkSyncIndexForShare(ae::ObjId share_id) const;

 private:
  std::vector<LocalPtr<LinkSyncState>> rebuild_local_sync_stash_;
};

class AddShareEvent : public EventFor<SharedNode, AddShareEvent> {
  APPTRAVERSE_OBJECT(AddShareEvent, Event, 1)

 protected:
  AddShareEvent() = default;

 public:
  explicit AddShareEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(link), AE_MMBR(access), AE_MMBR(share_id))

  // v0 journals predate the protocol field. Their relationship id is the
  // event object itself. v1 carries share_id so a receiver can remap the
  // event object without minting a new relationship.
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, link, access);
    share_id = obj_id;
  }

  template <typename Dnv>
  void Load(ae::Version<1>, Dnv& dnv) {
    dnv(base_, link, access, share_id);
  }

  template <typename Dnv>
  void Save(ae::Version<1>, Dnv& dnv) const {
    dnv(base_, link, access, share_id);
  }

  Link::ptr link;
  std::uint8_t access{
      static_cast<std::uint8_t>(ShareAccess::ReadWrite)};
  ae::ObjId share_id;
};

// Release-safe check for a restored working permanent-pair dialog before
// RegisterNode / send. Empty string: OK, or not yet a working pair (0–1
// shares: empty base or mid InstallLocalShare). Non-empty: diagnostic; caller
// must not mutate storage or truncate participants.
std::string DescribeRestoredPermanentPairViolation(
    SharedNode const& node, std::string const& local_endpoint);

}  // namespace apptraverse

#endif  // APPTRAVERSE_SHARED_NODE_H_
