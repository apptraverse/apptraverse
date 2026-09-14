#ifndef APPTRAVERSE_SHARED_NODE_H_
#define APPTRAVERSE_SHARED_NODE_H_

#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

#include "aether-miscpp/reflect/reflect.h"

#include "apptraverse/event_for.h"
#include "apptraverse/link.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_link.h"
#include "apptraverse/object_macros.h"

namespace apptraverse {

enum class ShareAccess : std::uint8_t {
  ReadWrite = 0,
  ReadOnly = 1,
};

// One lifetime of SharedNode <-> Link. share_id is the relationship identity:
// the ObjId of the AddShareEvent that opened it, so remove + re-add over the
// same Link yields a different relationship. Shared topology state.
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

// Local-persistent synchronization progress for one Share relationship of one
// SharedNode. Belongs to share_id, not to the Link: a later relationship over
// the same Link gets its own state.
// Event-sourced Node: phase changes go through Commit/Apply. Reachable from
// SharedNode only via LocalPtr, so network shared-graph serialization excludes
// it without SharedNode-specific sanitization.
class LinkSyncState : public NodeFor<LinkSyncState> {
  APPTRAVERSE_OBJECT(LinkSyncState, Node, 1)

 protected:
  LinkSyncState() = default;

 public:
  explicit LinkSyncState(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(share_id), AE_MMBR(link),
                    AE_MMBR(initial_sync_phase),
                    AE_MMBR(pending_initial_packet_id),
                    AE_MMBR(pending_initial_packet),
                    AE_MMBR(received_initial_packet_id))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv&) {
    throw std::runtime_error("LinkSyncState v0 is not supported");
  }

  template <typename Dnv>
  void Load(ae::Version<1>, Dnv& dnv) {
    Node::Load(ae::Version<3>{}, dnv);
    dnv(share_id, link, initial_sync_phase, pending_initial_packet_id,
        pending_initial_packet, received_initial_packet_id);
  }

  template <typename Dnv>
  void Save(ae::Version<1>, Dnv& dnv) const {
    Node::Save(ae::Version<3>{}, dnv);
    dnv(share_id, link, initial_sync_phase, pending_initial_packet_id,
        pending_initial_packet, received_initial_packet_id);
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

  InitialSyncPhase GetInitialSyncPhase() const {
    return static_cast<InitialSyncPhase>(initial_sync_phase);
  }

  void SetInitialSyncPhase(InitialSyncPhase phase);
  void CompleteInitialSync();
  void NoteInitialSyncReceived(ae::ObjId packet_id);

  void Apply(SetLinkInitialSyncPhaseEvent const& event);
  void Apply(BeginInitialSyncEvent const& event);
  void Apply(CompleteInitialSyncEvent const& event);
  void Apply(NoteInitialSyncReceivedEvent const& event);
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

  AE_OBJECT_REFLECT(AE_MMBR(packet))

  std::vector<std::uint8_t> packet;
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

class AddShareEvent;
class RemoveShareEvent;
class ChangeShareAccessEvent;

// Generic shared Node: shared topology (shares[]) plus local-persistent
// per-Link sync metadata (link_sync_states via LocalPtr).
class SharedNode : public NodeFor<SharedNode> {
  APPTRAVERSE_OBJECT(SharedNode, Node, 1)

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
  void Load(ae::Version<1>, Dnv& dnv) {
    Node::Load(ae::Version<3>{}, dnv);
    dnv(shares, link_sync_states);
  }

  template <typename Dnv>
  void Save(ae::Version<1>, Dnv& dnv) const {
    Node::Save(ae::Version<3>{}, dnv);
    dnv(shares, link_sync_states);
  }

  std::vector<Share> shares;
  std::vector<LocalPtr<LinkSyncState>> link_sync_states;

  // Live topology mutations go through Events.
  void AddShare(Link::ptr link, ShareAccess access);
  void RemoveShare(Link::ptr link);
  void SetShareAccess(Link::ptr link, ShareAccess access);

  // Local sync phase changes are Events on the LinkSyncState Node of the
  // active Share relationship (created by AddShare Apply). Not shared Events.
  void SetInitialSyncPhase(Link::ptr link, InitialSyncPhase phase);
  InitialSyncPhase GetInitialSyncPhase(Link::ptr link) const;

  void Apply(AddShareEvent const& event);
  void Apply(RemoveShareEvent const& event);
  void Apply(ChangeShareAccessEvent const& event);

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
  APPTRAVERSE_OBJECT(AddShareEvent, Event, 0)

 protected:
  AddShareEvent() = default;

 public:
  explicit AddShareEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(link), AE_MMBR(access))

  Link::ptr link;
  std::uint8_t access{
      static_cast<std::uint8_t>(ShareAccess::ReadWrite)};
};

// Names the Share relationship being closed, not the transport endpoint: the
// same Link may have been shared and unshared several times.
class RemoveShareEvent : public EventFor<SharedNode, RemoveShareEvent> {
  APPTRAVERSE_OBJECT(RemoveShareEvent, Event, 0)

 protected:
  RemoveShareEvent() = default;

 public:
  explicit RemoveShareEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(share_id))

  ae::ObjId share_id;
};

class ChangeShareAccessEvent
    : public EventFor<SharedNode, ChangeShareAccessEvent> {
  APPTRAVERSE_OBJECT(ChangeShareAccessEvent, Event, 0)

 protected:
  ChangeShareAccessEvent() = default;

 public:
  explicit ChangeShareAccessEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(share_id), AE_MMBR(access))

  ae::ObjId share_id;
  std::uint8_t access{
      static_cast<std::uint8_t>(ShareAccess::ReadWrite)};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_SHARED_NODE_H_
