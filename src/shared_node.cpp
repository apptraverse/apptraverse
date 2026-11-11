#include "apptraverse/shared_node.h"

#include <algorithm>
#include <utility>

namespace apptraverse {

APPTRAVERSE_REGISTER(SharedLinkDescriptor);
APPTRAVERSE_REGISTER(SharedPeerState);
APPTRAVERSE_REGISTER(MarkSharedEventDeliveredEvent);
APPTRAVERSE_REGISTER(QueueSharedPacketEvent);
APPTRAVERSE_REGISTER(CompleteSharedPacketEvent);
APPTRAVERSE_REGISTER(RememberSharedInitialEvent);
APPTRAVERSE_REGISTER(SharedNodeLocalState);
APPTRAVERSE_REGISTER(AddSharedPeerStateEvent);
APPTRAVERSE_REGISTER(NoteSharedLocalEventEvent);
APPTRAVERSE_REGISTER(SharedNode);
APPTRAVERSE_REGISTER(AddSharedLinkEvent);

void EnsureSharedNodeRegistration() {
  EnsureCoreObjectRegistration();
  (void)&g_apptraverse_registrar_SharedLinkDescriptor;
  (void)&g_apptraverse_registrar_SharedPeerState;
  (void)&g_apptraverse_registrar_MarkSharedEventDeliveredEvent;
  (void)&g_apptraverse_registrar_QueueSharedPacketEvent;
  (void)&g_apptraverse_registrar_CompleteSharedPacketEvent;
  (void)&g_apptraverse_registrar_RememberSharedInitialEvent;
  (void)&g_apptraverse_registrar_SharedNodeLocalState;
  (void)&g_apptraverse_registrar_AddSharedPeerStateEvent;
  (void)&g_apptraverse_registrar_NoteSharedLocalEventEvent;
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
  AddDelivered(event.event_id);
}

void SharedPeerState::AddDelivered(ae::ObjId event_id) {
  if (!HasDelivered(event_id)) {
    delivered_events.push_back(event_id);
  }
}

SharedPendingPacket const* SharedPeerState::FindPending(
    ae::ObjId packet_id) const {
  auto const found = std::find_if(pending_packets.begin(), pending_packets.end(),
      [packet_id](auto const& packet) { return packet.packet_id == packet_id; });
  return found == pending_packets.end() ? nullptr : &*found;
}

void SharedPeerState::QueuePacket(SharedPendingPacket packet) {
  if (FindPending(packet.packet_id) != nullptr) {
    return;
  }
  auto event = QueueSharedPacketEvent::ptr::Create(ae::CreateWith{*domain});
  event->packet = std::move(packet);
  Commit(std::move(event));
}

bool SharedPeerState::CanApply(QueueSharedPacketEvent const& event) const {
  auto const& packet = event.packet;
  return packet.packet_id.is_valid() && !packet.bytes.empty() &&
         (packet.initial || packet.event_id.is_valid()) &&
         FindPending(packet.packet_id) == nullptr &&
         std::all_of(packet.covered_events.begin(), packet.covered_events.end(),
                     [](ae::ObjId id) { return id.is_valid(); });
}

void SharedPeerState::Apply(QueueSharedPacketEvent const& event) {
  pending_packets.push_back(event.packet);
}

void SharedPeerState::CompletePacket(ae::ObjId packet_id) {
  if (FindPending(packet_id) == nullptr) {
    return;
  }
  auto event = CompleteSharedPacketEvent::ptr::Create(ae::CreateWith{*domain});
  event->packet_id = packet_id;
  Commit(std::move(event));
}

bool SharedPeerState::CanApply(CompleteSharedPacketEvent const& event) const {
  return FindPending(event.packet_id) != nullptr;
}

void SharedPeerState::Apply(CompleteSharedPacketEvent const& event) {
  auto const pending = std::find_if(pending_packets.begin(), pending_packets.end(),
      [&](auto const& packet) { return packet.packet_id == event.packet_id; });
  assert(pending != pending_packets.end());
  if (pending->initial) {
    initial_complete = true;
    for (auto id : pending->covered_events) {
      AddDelivered(id);
    }
  } else {
    AddDelivered(pending->event_id);
  }
  pending_packets.erase(pending);
}

bool SharedPeerState::HasReceivedInitial(ae::ObjId packet_id) const {
  return std::find(received_initial_packets.begin(),
                   received_initial_packets.end(), packet_id) !=
         received_initial_packets.end();
}

void SharedPeerState::RememberInitial(
    ae::ObjId packet_id, std::vector<ae::ObjId> covered_events) {
  if (HasReceivedInitial(packet_id)) {
    return;
  }
  auto event = RememberSharedInitialEvent::ptr::Create(ae::CreateWith{*domain});
  event->packet_id = packet_id;
  event->covered_events = std::move(covered_events);
  Commit(std::move(event));
}

bool SharedPeerState::CanApply(RememberSharedInitialEvent const& event) const {
  return event.packet_id.is_valid() && !HasReceivedInitial(event.packet_id) &&
         std::all_of(event.covered_events.begin(), event.covered_events.end(),
                     [](ae::ObjId id) { return id.is_valid(); });
}

void SharedPeerState::Apply(RememberSharedInitialEvent const& event) {
  received_initial_packets.push_back(event.packet_id);
  for (auto id : event.covered_events) {
    AddDelivered(id);
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

void SharedNodeLocalState::NoteLocalEvent(ae::ObjId event_id) {
  assert(event_id.is_valid());
  if (std::find(local_events.begin(), local_events.end(), event_id) !=
      local_events.end()) {
    return;
  }
  auto event = NoteSharedLocalEventEvent::ptr::Create(ae::CreateWith{*domain});
  event->event_id = event_id;
  Commit(std::move(event));
}

bool SharedNodeLocalState::CanApply(NoteSharedLocalEventEvent const& event) const {
  return event.event_id.is_valid();
}

void SharedNodeLocalState::Apply(NoteSharedLocalEventEvent const& event) {
  if (std::find(local_events.begin(), local_events.end(), event.event_id) ==
      local_events.end()) {
    local_events.push_back(event.event_id);
  }
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
