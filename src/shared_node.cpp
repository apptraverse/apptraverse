#include "apptraverse/shared_node.h"

#include <algorithm>

#include "apptraverse/object_macros.h"
#include "apptraverse/runtime_node.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(LinkSyncState);
APPTRAVERSE_REGISTER(SetLinkInitialSyncPhaseEvent);
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

void LinkSyncState::Apply(SetLinkInitialSyncPhaseEvent const& event) {
  initial_sync_phase = event.phase;
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

std::size_t SharedNode::FindLinkSyncIndex(ae::ObjId link_id) const {
  for (std::size_t i = 0; i < link_sync_states.size(); ++i) {
    auto const& entry = link_sync_states[i];
    if (!entry.is_valid()) {
      continue;
    }
    if (!entry.is_loaded()) {
      entry.Load();
    }
    if (entry->link.is_valid() && entry->link.id() == link_id) {
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
  Commit(event);
}

void SharedNode::RemoveShare(Link::ptr link) {
  assert(link.is_valid() && "RemoveShare requires a valid Link");
  if (FindShareIndex(link.id()) >= shares.size()) {
    return;
  }
  auto event = RemoveShareEvent::ptr::Create(ae::CreateWith{*domain});
  event->link = std::move(link);
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
  event->link = std::move(link);
  event->access = static_cast<std::uint8_t>(access);
  Commit(event);
}

void SharedNode::Apply(AddShareEvent const& event) {
  assert(event.link.is_valid());
  if (FindShareIndex(event.link.id()) < shares.size()) {
    return;
  }
  shares.push_back(Share{.link = event.link, .access = event.access});

  // New share relationship starts NotStarted. Skip if local sync for this Link
  // already exists (e.g. stash restored across RebuildFromBaseAndReplay before
  // journal replay re-applies AddShare).
  if (FindLinkSyncIndex(event.link.id()) >= link_sync_states.size()) {
    auto state = LinkSyncState::ptr::Create(ae::CreateWith{*domain});
    // Creation-time immutable config before the Node becomes live.
    state->link = event.link;
    state->initial_sync_phase =
        static_cast<std::uint8_t>(InitialSyncPhase::NotStarted);
    InitializeRuntimeNode(*state);
    link_sync_states.push_back(LocalPtr<LinkSyncState>{state});
  }

  NoteMaterializedChange();
}

void SharedNode::Apply(RemoveShareEvent const& event) {
  assert(event.link.is_valid());
  auto const index = FindShareIndex(event.link.id());
  if (index >= shares.size()) {
    return;
  }
  shares.erase(shares.begin() + static_cast<std::ptrdiff_t>(index));

  // Drop stale local sync for this Link. A later AddShare creates a fresh
  // NotStarted relationship and must not inherit Complete.
  auto const sync_index = FindLinkSyncIndex(event.link.id());
  if (sync_index < link_sync_states.size()) {
    link_sync_states.erase(link_sync_states.begin() +
                           static_cast<std::ptrdiff_t>(sync_index));
  }

  NoteMaterializedChange();
}

void SharedNode::Apply(ChangeShareAccessEvent const& event) {
  assert(event.link.is_valid());
  auto const index = FindShareIndex(event.link.id());
  assert(index < shares.size());
  shares[index].access = event.access;
  NoteMaterializedChange();
}

void SharedNode::SetInitialSyncPhase(Link::ptr link, InitialSyncPhase phase) {
  assert(link.is_valid());
  auto const index = FindLinkSyncIndex(link.id());
  assert(index < link_sync_states.size() &&
         "SetInitialSyncPhase requires sync state from an existing share");
  link_sync_states[index]->SetInitialSyncPhase(phase);
}

InitialSyncPhase SharedNode::GetInitialSyncPhase(Link::ptr link) const {
  assert(link.is_valid());
  auto const index = FindLinkSyncIndex(link.id());
  if (index >= link_sync_states.size()) {
    return InitialSyncPhase::NotStarted;
  }
  return link_sync_states[index]->GetInitialSyncPhase();
}

void SharedNode::StashLocalPersistentAcrossRebuild() {
  rebuild_local_sync_stash_ = link_sync_states;
}

void SharedNode::RestoreLocalPersistentAcrossRebuild() {
  link_sync_states = std::move(rebuild_local_sync_stash_);
  rebuild_local_sync_stash_.clear();
}

}  // namespace apptraverse
