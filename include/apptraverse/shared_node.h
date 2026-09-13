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

struct Share {
  Link::ptr link;
  std::uint8_t access{
      static_cast<std::uint8_t>(ShareAccess::ReadWrite)};

  AE_REFLECT_MEMBERS(link, access)

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

// Local-persistent per-Link synchronization progress for one SharedNode.
// Lives as a separate Obj so SharedNode journal rebuild reuses the live
// Domain instance via LocalPtr (Domain::Find) instead of rolling back phase.
class LinkSyncState : public ae::Obj {
  APPTRAVERSE_OBJECT(LinkSyncState, ae::Obj, 0)

 protected:
  LinkSyncState() = default;

 public:
  explicit LinkSyncState(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(link), AE_MMBR(initial_sync_phase))

  Link::ptr link;
  std::uint8_t initial_sync_phase{
      static_cast<std::uint8_t>(InitialSyncPhase::NotStarted)};

  InitialSyncPhase GetInitialSyncPhase() const {
    return static_cast<InitialSyncPhase>(initial_sync_phase);
  }

  void SetInitialSyncPhase(InitialSyncPhase phase) {
    initial_sync_phase = static_cast<std::uint8_t>(phase);
  }
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
    Node::Load(ae::Version<2>{}, dnv);
    dnv(shares, link_sync_states);
  }

  template <typename Dnv>
  void Save(ae::Version<1>, Dnv& dnv) const {
    Node::Save(ae::Version<2>{}, dnv);
    dnv(shares, link_sync_states);
  }

  std::vector<Share> shares;
  std::vector<LocalPtr<LinkSyncState>> link_sync_states;

  // Live topology mutations go through Events.
  void AddShare(Link::ptr link, ShareAccess access);
  void RemoveShare(Link::ptr link);
  void SetShareAccess(Link::ptr link, ShareAccess access);

  // Local-only sync bookkeeping (not shared Events).
  LinkSyncState::ptr EnsureLinkSyncState(Link::ptr link);
  void SetInitialSyncPhase(Link::ptr link, InitialSyncPhase phase);
  InitialSyncPhase GetInitialSyncPhase(Link::ptr link) const;

  // Network shared-graph preparation: drop local-persistent edges on this
  // instance (used on a scratch copy; does not clear the live source).
  void ClearLocalPersistentEdges();

  void Apply(AddShareEvent const& event);
  void Apply(RemoveShareEvent const& event);
  void Apply(ChangeShareAccessEvent const& event);

  void StashLocalPersistentAcrossRebuild() override;
  void RestoreLocalPersistentAcrossRebuild() override;

  std::size_t FindShareIndex(ae::ObjId link_id) const;
  std::size_t FindLinkSyncIndex(ae::ObjId link_id) const;

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

class RemoveShareEvent : public EventFor<SharedNode, RemoveShareEvent> {
  APPTRAVERSE_OBJECT(RemoveShareEvent, Event, 0)

 protected:
  RemoveShareEvent() = default;

 public:
  explicit RemoveShareEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(link))

  Link::ptr link;
};

class ChangeShareAccessEvent
    : public EventFor<SharedNode, ChangeShareAccessEvent> {
  APPTRAVERSE_OBJECT(ChangeShareAccessEvent, Event, 0)

 protected:
  ChangeShareAccessEvent() = default;

 public:
  explicit ChangeShareAccessEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(link), AE_MMBR(access))

  Link::ptr link;
  std::uint8_t access{
      static_cast<std::uint8_t>(ShareAccess::ReadWrite)};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_SHARED_NODE_H_
