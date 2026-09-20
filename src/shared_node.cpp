#include "apptraverse/shared_node.h"

#include <algorithm>

#include "apptraverse/object_macros.h"
#include "apptraverse/runtime_node.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(LinkSyncState);
APPTRAVERSE_REGISTER(SetLinkInitialSyncPhaseEvent);
APPTRAVERSE_REGISTER(BeginInitialSyncEvent);
APPTRAVERSE_REGISTER(CompleteInitialSyncEvent);
APPTRAVERSE_REGISTER(NoteInitialSyncReceivedEvent);
APPTRAVERSE_REGISTER(CompleteFromReceivedSnapshotEvent);
APPTRAVERSE_REGISTER(NotePeerDeliveredEvent);
APPTRAVERSE_REGISTER(BeginIncrementalEventSyncEvent);
APPTRAVERSE_REGISTER(CompleteIncrementalEventSyncEvent);
APPTRAVERSE_REGISTER(SharedNode);
APPTRAVERSE_REGISTER(AddShareEvent);
APPTRAVERSE_REGISTER(RemoveShareEvent);
APPTRAVERSE_REGISTER(ChangeShareAccessEvent);

}  // namespace

void ForceSharedNodeRegistration() {}

void LinkSyncState::SetInitialSyncPhase(InitialSyncPhase phase) {
  if (GetInitialSyncPhase() == phase) {
    return;
  }
  auto event =
      SetLinkInitialSyncPhaseEvent::ptr::Create(ae::CreateWith{*domain});
  event->phase = static_cast<std::uint8_t>(phase);
  Commit(event);
}

void LinkSyncState::CompleteInitialSync() {
  auto event = CompleteInitialSyncEvent::ptr::Create(ae::CreateWith{*domain});
  Commit(event);
}

void LinkSyncState::NoteInitialSyncReceived(ae::ObjId packet_id) {
  assert(packet_id.is_valid());
  auto event =
      NoteInitialSyncReceivedEvent::ptr::Create(ae::CreateWith{*domain});
  event->packet_id = packet_id;
  Commit(event);
}

void LinkSyncState::Apply(SetLinkInitialSyncPhaseEvent const& event) {
  initial_sync_phase = event.phase;
  NoteMaterializedChange();
}

void LinkSyncState::Apply(BeginInitialSyncEvent const& event) {
  assert(GetInitialSyncPhase() == InitialSyncPhase::NotStarted);
  assert(!event.packet.empty());
  initial_sync_phase = static_cast<std::uint8_t>(InitialSyncPhase::Pending);
  pending_initial_packet_id = event.obj_id;
  pending_initial_packet = event.packet;
  pending_initial_covered_event_ids = event.covered_event_ids;
  NoteMaterializedChange();
}

void LinkSyncState::Apply(CompleteInitialSyncEvent const&) {
  initial_sync_phase = static_cast<std::uint8_t>(InitialSyncPhase::Complete);
  pending_initial_packet_id = ae::ObjId{};
  pending_initial_packet.clear();
  for (auto const& identity : pending_initial_covered_event_ids) {
    if (!HasDelivered(identity)) {
      delivered_event_ids.push_back(identity);
    }
  }
  pending_initial_covered_event_ids.clear();
  NoteMaterializedChange();
}

void LinkSyncState::Apply(NoteInitialSyncReceivedEvent const& event) {
  received_initial_packet_id = event.packet_id;
  initial_sync_phase = static_cast<std::uint8_t>(InitialSyncPhase::Complete);
  NoteMaterializedChange();
}

void LinkSyncState::CompleteFromReceivedSnapshot(
    std::vector<SharedEventId> delivered) {
  auto event =
      CompleteFromReceivedSnapshotEvent::ptr::Create(ae::CreateWith{*domain});
  event->delivered_event_ids = std::move(delivered);
  Commit(event);
}

void LinkSyncState::NotePeerDelivered(std::vector<SharedEventId> delivered) {
  bool any_new = false;
  for (auto const& identity : delivered) {
    if (!identity.origin_uid.empty() && !HasDelivered(identity)) {
      any_new = true;
      break;
    }
  }
  if (!any_new) {
    return;
  }
  auto event = NotePeerDeliveredEvent::ptr::Create(ae::CreateWith{*domain});
  event->delivered_event_ids = std::move(delivered);
  Commit(event);
}

bool LinkSyncState::CanApply(
    CompleteFromReceivedSnapshotEvent const& event) const {
  (void)event;
  return GetInitialSyncPhase() == InitialSyncPhase::NotStarted &&
         !HasPendingEvent();
}

void LinkSyncState::Apply(CompleteFromReceivedSnapshotEvent const& event) {
  initial_sync_phase = static_cast<std::uint8_t>(InitialSyncPhase::Complete);
  for (auto const& identity : event.delivered_event_ids) {
    if (!identity.origin_uid.empty() && !HasDelivered(identity)) {
      delivered_event_ids.push_back(identity);
    }
  }
  pending_initial_packet_id = ae::ObjId{};
  pending_initial_packet.clear();
  pending_initial_covered_event_ids.clear();
  NoteMaterializedChange();
}

void LinkSyncState::Apply(NotePeerDeliveredEvent const& event) {
  for (auto const& identity : event.delivered_event_ids) {
    if (!identity.origin_uid.empty() && !HasDelivered(identity)) {
      delivered_event_ids.push_back(identity);
    }
  }
  NoteMaterializedChange();
}

void LinkSyncState::BeginIncrementalEvent(SharedEventId identity,
                                          std::vector<std::uint8_t> packet) {
  assert(GetInitialSyncPhase() == InitialSyncPhase::Complete);
  assert(!HasPendingEvent());
  assert(!identity.origin_uid.empty());
  assert(identity.origin_sequence != 0);
  assert(!packet.empty());
  auto event =
      BeginIncrementalEventSyncEvent::ptr::Create(ae::CreateWith{*domain});
  event->identity = std::move(identity);
  event->packet = std::move(packet);
  Commit(event);
}

void LinkSyncState::CompleteIncrementalEvent() {
  assert(HasPendingEvent());
  auto event =
      CompleteIncrementalEventSyncEvent::ptr::Create(ae::CreateWith{*domain});
  Commit(event);
}

bool LinkSyncState::HasDelivered(SharedEventId const& identity) const {
  for (auto const& delivered : delivered_event_ids) {
    if (delivered == identity) {
      return true;
    }
  }
  return false;
}

void LinkSyncState::Apply(BeginIncrementalEventSyncEvent const& event) {
  assert(GetInitialSyncPhase() == InitialSyncPhase::Complete);
  assert(!HasPendingEvent());
  assert(!event.identity.origin_uid.empty());
  assert(!event.packet.empty());
  pending_event_packet_id = event.obj_id;
  pending_event_identity = event.identity;
  pending_event_packet = event.packet;
  NoteMaterializedChange();
}

void LinkSyncState::Apply(CompleteIncrementalEventSyncEvent const&) {
  assert(HasPendingEvent());
  if (!HasDelivered(pending_event_identity)) {
    delivered_event_ids.push_back(pending_event_identity);
  }
  pending_event_packet_id = ae::ObjId{};
  pending_event_identity = {};
  pending_event_packet.clear();
  NoteMaterializedChange();
}

std::size_t SharedNode::FindShareIndex(ae::ObjId link_id) const {
  for (std::size_t i = 0; i < shares.size(); ++i) {
    if (shares[i].link.is_valid() && shares[i].link.id() == link_id) {
      return i;
    }
  }
  return shares.size();
}

std::size_t SharedNode::FindShareIndexForShare(ae::ObjId share_id) const {
  for (std::size_t i = 0; i < shares.size(); ++i) {
    if (shares[i].share_id == share_id) {
      return i;
    }
  }
  return shares.size();
}

std::size_t SharedNode::FindLinkSyncIndexForShare(ae::ObjId share_id) const {
  for (std::size_t i = 0; i < link_sync_states.size(); ++i) {
    auto const& entry = link_sync_states[i];
    if (!entry.is_valid()) {
      continue;
    }
    if (!entry.is_loaded()) {
      entry.Load();
    }
    if (entry->share_id == share_id) {
      return i;
    }
  }
  return link_sync_states.size();
}

void SharedNode::AddShare(Link::ptr link, ShareAccess access) {
  assert(link.is_valid() && "AddShare requires a valid Link");
  // Idempotent: same Link already shared → no duplicate entry.
  if (FindShareIndex(link.id()) < shares.size()) {
    return;
  }
  auto event = AddShareEvent::ptr::Create(ae::CreateWith{*domain});
  event->link = std::move(link);
  event->access = static_cast<std::uint8_t>(access);
  // Protocol identity. A later import must keep this value rather than the
  // receiver-local event object id.
  event->share_id = event.id();
  Commit(event);
}

void SharedNode::RemoveShare(Link::ptr link) {
  assert(link.is_valid() && "RemoveShare requires a valid Link");
  auto const index = FindShareIndex(link.id());
  if (index >= shares.size()) {
    return;
  }
  auto event = RemoveShareEvent::ptr::Create(ae::CreateWith{*domain});
  event->share_id = shares[index].share_id;
  Commit(event);
}

void SharedNode::SetShareAccess(Link::ptr link, ShareAccess access) {
  assert(link.is_valid() && "SetShareAccess requires a valid Link");
  auto const index = FindShareIndex(link.id());
  assert(index < shares.size() && "SetShareAccess requires an existing share");
  if (shares[index].GetAccess() == access) {
    return;
  }
  auto event = ChangeShareAccessEvent::ptr::Create(ae::CreateWith{*domain});
  event->share_id = shares[index].share_id;
  event->access = static_cast<std::uint8_t>(access);
  Commit(event);
}

bool SharedNode::ShareIntroduced(ae::ObjId share_id) const {
  if (!share_id.is_valid()) {
    return false;
  }
  if (FindShareIndexForShare(share_id) < shares.size()) {
    return true;
  }
  for (auto const& record : journal) {
    if (!record.event.is_valid()) {
      continue;
    }
    auto event = record.event;
    if (!event.is_loaded()) {
      event.Load();
    }
    if (!event.is_loaded() || event->GetClassId() != AddShareEvent::kClassId) {
      continue;
    }
    if (static_cast<AddShareEvent const&>(*event).share_id == share_id) {
      return true;
    }
  }
  return false;
}

bool SharedNode::CanApply(AddShareEvent const& event) const {
  if (!event.share_id.is_valid() || !event.link.is_valid()) {
    return false;
  }
  if (FindShareIndexForShare(event.share_id) < shares.size()) {
    return false;
  }
  return FindShareIndex(event.link.id()) >= shares.size();
}

bool SharedNode::CanApply(RemoveShareEvent const& event) const {
  // Open share, or already closed after a prior admissible remove of the
  // same lifetime. An id that was never introduced is not a concurrent case.
  return ShareIntroduced(event.share_id);
}

bool SharedNode::CanApply(ChangeShareAccessEvent const& event) const {
  if (event.access > static_cast<std::uint8_t>(ShareAccess::ReadOnly)) {
    return false;
  }
  return ShareIntroduced(event.share_id);
}

void SharedNode::Apply(AddShareEvent const& event) {
  assert(CanApply(event));
  assert(event.link.is_valid());
  assert(event.share_id.is_valid());
  auto const share_id = event.share_id;
  shares.push_back(Share{
      .share_id = share_id, .link = event.link, .access = event.access});

  // Local sync belongs to this relationship. It already exists when the stash
  // restored it across RebuildFromBaseAndReplay before replay re-applied this
  // Event; a historical relationship over the same Link gets its own state.
  if (FindLinkSyncIndexForShare(share_id) >= link_sync_states.size()) {
    auto state = LinkSyncState::ptr::Create(ae::CreateWith{*domain});
    // Creation-time immutable config before the Node becomes live.
    state->share_id = share_id;
    state->link = event.link;
    state->initial_sync_phase =
        static_cast<std::uint8_t>(InitialSyncPhase::NotStarted);
    InitializeRuntimeNode(*state);
    link_sync_states.push_back(LocalPtr<LinkSyncState>{state});
  }

  NoteMaterializedChange();
}

void SharedNode::Apply(RemoveShareEvent const& event) {
  assert(CanApply(event));
  auto const index = FindShareIndexForShare(event.share_id);
  if (index >= shares.size()) {
    // Already closed by a concurrent remove of the same lifetime.
    return;
  }
  shares.erase(shares.begin() + static_cast<std::ptrdiff_t>(index));

  // Keep LinkSyncState: it is the durable delivery record for the closing
  // event and for acknowledging retransmits after the share row is gone.
  // A later AddShare over the same Link uses a new share_id and a new state.

  NoteMaterializedChange();
}

void SharedNode::Apply(ChangeShareAccessEvent const& event) {
  assert(CanApply(event));
  auto const index = FindShareIndexForShare(event.share_id);
  if (index >= shares.size()) {
    // Closed by a concurrent remove. Do not reopen.
    return;
  }
  shares[index].access = event.access;
  NoteMaterializedChange();
}

void SharedNode::SetInitialSyncPhase(Link::ptr link, InitialSyncPhase phase) {
  assert(link.is_valid());
  auto const share_index = FindShareIndex(link.id());
  assert(share_index < shares.size() &&
         "SetInitialSyncPhase requires an existing share");
  auto const index =
      FindLinkSyncIndexForShare(shares[share_index].share_id);
  assert(index < link_sync_states.size() &&
         "SetInitialSyncPhase requires sync state of the active relationship");
  link_sync_states[index]->SetInitialSyncPhase(phase);
}

InitialSyncPhase SharedNode::GetInitialSyncPhase(Link::ptr link) const {
  assert(link.is_valid());
  auto const share_index = FindShareIndex(link.id());
  if (share_index >= shares.size()) {
    return InitialSyncPhase::NotStarted;
  }
  auto const index =
      FindLinkSyncIndexForShare(shares[share_index].share_id);
  if (index >= link_sync_states.size()) {
    // Network-imported topology: shared Share without local sync state yet.
    return InitialSyncPhase::NotStarted;
  }
  return link_sync_states[index]->GetInitialSyncPhase();
}

void SharedNode::StashLocalPersistentAcrossRebuild() {
  rebuild_local_sync_stash_.clear();
  for (auto const& entry : link_sync_states) {
    // An invalid entry is the absence of local state, not state to carry: a
    // network-imported SharedNode has one per Share. Replay then creates this
    // replica's own LinkSyncState for each relationship.
    if (entry.is_valid()) {
      rebuild_local_sync_stash_.push_back(entry);
    }
  }
}

void SharedNode::RestoreLocalPersistentAcrossRebuild() {
  link_sync_states = std::move(rebuild_local_sync_stash_);
  rebuild_local_sync_stash_.clear();
}

}  // namespace apptraverse
