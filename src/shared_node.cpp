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
APPTRAVERSE_REGISTER(BeginIncrementalEventSyncEvent);
APPTRAVERSE_REGISTER(CompleteIncrementalEventSyncEvent);
APPTRAVERSE_REGISTER(SharedNode);
APPTRAVERSE_REGISTER(AddShareEvent);

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

bool LinkSyncState::CanApply(BeginIncrementalEventSyncEvent const& event) const {
  if (GetInitialSyncPhase() != InitialSyncPhase::Complete) {
    return false;
  }
  if (HasPendingEvent()) {
    return false;
  }
  if (event.identity.origin_uid.empty() || event.packet.empty()) {
    return false;
  }
  return true;
}

void LinkSyncState::Apply(BeginIncrementalEventSyncEvent const& event) {
  assert(CanApply(event));
  pending_event_packet_id = event.obj_id;
  pending_event_identity = event.identity;
  pending_event_packet = event.packet;
  NoteMaterializedChange();
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

void SharedNode::InstallLocalShare(Link::ptr link, ShareAccess access) {
  assert(link.is_valid() && "InstallLocalShare requires a valid Link");
  assert(access == ShareAccess::ReadWrite &&
         "InstallLocalShare requires ReadWrite");
  // Idempotent: same Link already shared → no duplicate entry.
  if (FindShareIndex(link.id()) < shares.size()) {
    return;
  }
  // Permanent pair: exactly two participants. Refuse a third.
  if (shares.size() >= 2) {
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

bool SharedNode::CanApply(AddShareEvent const& event) const {
  if (!event.share_id.is_valid() || !event.link.is_valid()) {
    return false;
  }
  if (event.access != static_cast<std::uint8_t>(ShareAccess::ReadWrite)) {
    return false;
  }
  if (shares.size() >= 2) {
    return false;
  }
  if (FindShareIndexForShare(event.share_id) < shares.size()) {
    return false;
  }
  return FindShareIndex(event.link.id()) >= shares.size();
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

std::string DescribeRestoredPermanentPairViolation(
    SharedNode const& node, std::string const& local_endpoint) {
  // Empty base / mid InstallLocalShare: not a restored working dialog yet.
  if (node.shares.size() < 2) {
    return {};
  }
  if (node.shares.size() != 2) {
    return "Incompatible stored dialog: permanent pair requires exactly two "
           "participants, found " +
           std::to_string(node.shares.size());
  }
  if (local_endpoint.empty()) {
    return "Incompatible stored dialog: local endpoint is empty";
  }

  std::string endpoints[2];
  bool local_found = false;
  for (std::size_t i = 0; i < 2; ++i) {
    auto const& share = node.shares[i];
    if (!share.share_id.is_valid()) {
      return "Incompatible stored dialog: share relationship id is missing";
    }
    if (node.FindShareIndexForShare(share.share_id) != i) {
      return "Incompatible stored dialog: duplicate or inconsistent share "
             "relationship id";
    }
    if (share.GetAccess() != ShareAccess::ReadWrite) {
      return "Incompatible stored dialog: both participants must be ReadWrite";
    }
    if (!share.link.is_valid()) {
      return "Incompatible stored dialog: share link is missing";
    }
    if (!share.link.is_loaded()) {
      share.link.Load();
    }
    if (!share.link.is_loaded()) {
      return "Incompatible stored dialog: share link failed to load";
    }
    endpoints[i] = share.link->EndpointUid();
    if (endpoints[i].empty()) {
      return "Incompatible stored dialog: share endpoint is empty";
    }
    if (endpoints[i] == local_endpoint) {
      local_found = true;
    }

    auto const sync_index = node.FindLinkSyncIndexForShare(share.share_id);
    if (sync_index >= node.link_sync_states.size()) {
      return "Incompatible stored dialog: missing sync state for share "
             "relationship";
    }
    auto state = node.link_sync_states[sync_index];
    if (!state.is_valid()) {
      return "Incompatible stored dialog: invalid sync state for share "
             "relationship";
    }
    if (!state.is_loaded()) {
      state.Load();
    }
    if (!state.is_loaded()) {
      return "Incompatible stored dialog: sync state failed to load";
    }
    if (state->share_id != share.share_id) {
      return "Incompatible stored dialog: sync state share id mismatch";
    }
    if (!state->link.is_valid() || state->link.id() != share.link.id()) {
      return "Incompatible stored dialog: sync state link mismatch";
    }
  }

  if (endpoints[0] == endpoints[1]) {
    return "Incompatible stored dialog: participant endpoints must differ";
  }
  if (!local_found) {
    return "Incompatible stored dialog: neither participant matches local "
           "endpoint";
  }
  return {};
}

}  // namespace apptraverse
