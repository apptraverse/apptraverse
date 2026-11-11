#include "apptraverse/shared_node.h"

#include <algorithm>
#include <utility>

namespace apptraverse {

APPTRAVERSE_REGISTER(SharedLinkDescriptor);
APPTRAVERSE_REGISTER(SharedPeerState);
APPTRAVERSE_REGISTER(MarkSharedEventDeliveredEvent);
APPTRAVERSE_REGISTER(SharedNodeLocalState);
APPTRAVERSE_REGISTER(AddSharedPeerStateEvent);
APPTRAVERSE_REGISTER(SharedNode);
APPTRAVERSE_REGISTER(AddSharedLinkEvent);

void EnsureSharedNodeRegistration() {
  EnsureCoreObjectRegistration();
  (void)&g_apptraverse_registrar_SharedLinkDescriptor;
  (void)&g_apptraverse_registrar_SharedPeerState;
  (void)&g_apptraverse_registrar_MarkSharedEventDeliveredEvent;
  (void)&g_apptraverse_registrar_SharedNodeLocalState;
  (void)&g_apptraverse_registrar_AddSharedPeerStateEvent;
  (void)&g_apptraverse_registrar_SharedNode;
  (void)&g_apptraverse_registrar_AddSharedLinkEvent;
}

void SharedPeerState::MarkDelivered(ae::ObjId event_id) {
  assert(event_id.is_valid());
  if (HasDelivered(event_id)) {
    return;
  }
  auto event = MarkSharedEventDeliveredEvent::ptr::Create(ae::CreateWith{*domain});
  event->event_id = event_id;
  Commit(std::move(event));
}

bool SharedPeerState::HasDelivered(ae::ObjId event_id) const {
  return std::find(delivered_events.begin(), delivered_events.end(), event_id) !=
         delivered_events.end();
}

bool SharedPeerState::CanApply(MarkSharedEventDeliveredEvent const& event) const {
  return event.event_id.is_valid();
}

void SharedPeerState::Apply(MarkSharedEventDeliveredEvent const& event) {
  if (!HasDelivered(event.event_id)) {
    delivered_events.push_back(event.event_id);
  }
}

void SharedNodeLocalState::AddPeer(SharedPeerState::ptr peer) {
  assert(peer.is_valid() && peer.is_loaded() && peer.domain() == domain &&
         peer->link.is_valid() && peer->link.is_loaded() &&
         peer->link.domain() == domain && !peer->link->endpoint_uid.empty());
  auto const existing = std::find_if(peers.begin(), peers.end(),
      [&](auto const& candidate) {
        return candidate->link->endpoint_uid == peer->link->endpoint_uid;
      });
  if (existing != peers.end()) {
    return;
  }
  auto event = AddSharedPeerStateEvent::ptr::Create(ae::CreateWith{*domain});
  event->peer = std::move(peer);
  Commit(std::move(event));
}

bool SharedNodeLocalState::CanApply(AddSharedPeerStateEvent const& event) const {
  if (!event.peer.is_valid() || !event.peer.is_loaded() ||
      event.peer.domain() != domain || !event.peer->link.is_valid() ||
      !event.peer->link.is_loaded() || event.peer->link.domain() != domain ||
      event.peer->link->endpoint_uid.empty()) {
    return false;
  }
  return std::none_of(peers.begin(), peers.end(), [&](auto const& peer) {
    return peer->link->endpoint_uid == event.peer->link->endpoint_uid;
  });
}

void SharedNodeLocalState::Apply(AddSharedPeerStateEvent const& event) {
  peers.push_back(event.peer);
}

void SharedNode::AddLink(SharedLinkDescriptor::ptr link) {
  assert(link.is_valid() && link.is_loaded() && link.domain() == domain &&
         !link->endpoint_uid.empty());
  auto const existing = std::find_if(links.begin(), links.end(),
      [&](auto const& candidate) {
        return candidate->endpoint_uid == link->endpoint_uid;
      });
  if (existing != links.end()) {
    return;
  }
  auto event = AddSharedLinkEvent::ptr::Create(ae::CreateWith{*domain});
  event->link = std::move(link);
  Commit(std::move(event));
}

bool SharedNode::CanApply(AddSharedLinkEvent const& event) const {
  if (!event.link.is_valid() || !event.link.is_loaded() ||
      event.link.domain() != domain || event.link->endpoint_uid.empty()) {
    return false;
  }
  return std::none_of(links.begin(), links.end(), [&](auto const& link) {
    return link.id() == event.link.id() ||
           link->endpoint_uid == event.link->endpoint_uid;
  });
}

void SharedNode::Apply(AddSharedLinkEvent const& event) {
  links.push_back(event.link);
}

void SharedNode::StashLocalPersistentAcrossRebuild(void* rebuild_scratch) {
  auto& scratch = *static_cast<RebuildOperationScratch*>(rebuild_scratch);
  scratch.local_state = std::move(local_state);
}

void SharedNode::RestoreLocalPersistentAcrossRebuild(void* rebuild_scratch) {
  auto& scratch = *static_cast<RebuildOperationScratch*>(rebuild_scratch);
  local_state = std::move(scratch.local_state);
}

}  // namespace apptraverse
