#include "apptraverse/shared_node.h"

#include <algorithm>

#include "apptraverse/object_macros.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(LinkSyncState);
APPTRAVERSE_REGISTER(SharedNode);
APPTRAVERSE_REGISTER(AddShareEvent);
APPTRAVERSE_REGISTER(RemoveShareEvent);
APPTRAVERSE_REGISTER(ChangeShareAccessEvent);

}  // namespace

void ForceSharedNodeRegistration() {}

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
  NoteMaterializedChange();
}

void SharedNode::Apply(RemoveShareEvent const& event) {
  assert(event.link.is_valid());
  auto const index = FindShareIndex(event.link.id());
  if (index >= shares.size()) {
    return;
  }
  shares.erase(shares.begin() + static_cast<std::ptrdiff_t>(index));
  NoteMaterializedChange();
}

void SharedNode::Apply(ChangeShareAccessEvent const& event) {
  assert(event.link.is_valid());
  auto const index = FindShareIndex(event.link.id());
  assert(index < shares.size());
  shares[index].access = event.access;
  NoteMaterializedChange();
}

LinkSyncState::ptr SharedNode::EnsureLinkSyncState(Link::ptr link) {
  assert(link.is_valid());
  auto const index = FindLinkSyncIndex(link.id());
  if (index < link_sync_states.size()) {
    return link_sync_states[index].as_obj_ptr();
  }
  auto state = LinkSyncState::ptr::Create(ae::CreateWith{*domain});
  state->link = std::move(link);
  state->SetInitialSyncPhase(InitialSyncPhase::NotStarted);
  link_sync_states.push_back(LocalPtr<LinkSyncState>{state});
  state.Save();
  SharedNode::ptr self{domain, obj_id, {}, domain->Find(obj_id)};
  self.Save();
  return state;
}

void SharedNode::SetInitialSyncPhase(Link::ptr link, InitialSyncPhase phase) {
  auto state = EnsureLinkSyncState(std::move(link));
  state->SetInitialSyncPhase(phase);
  state.Save();
}

InitialSyncPhase SharedNode::GetInitialSyncPhase(Link::ptr link) const {
  assert(link.is_valid());
  auto const index = FindLinkSyncIndex(link.id());
  if (index >= link_sync_states.size()) {
    return InitialSyncPhase::NotStarted;
  }
  return link_sync_states[index]->GetInitialSyncPhase();
}

void SharedNode::ClearLocalPersistentEdges() {
  for (auto& entry : link_sync_states) {
    entry.Reset();
  }
  link_sync_states.clear();
}

void SharedNode::StashLocalPersistentAcrossRebuild() {
  rebuild_local_sync_stash_ = link_sync_states;
}

void SharedNode::RestoreLocalPersistentAcrossRebuild() {
  link_sync_states = std::move(rebuild_local_sync_stash_);
  rebuild_local_sync_stash_.clear();
}

}  // namespace apptraverse
