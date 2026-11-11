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
APPTRAVERSE_REGISTER(InstallSharedAdmissionEvent);
APPTRAVERSE_REGISTER(ConfirmSharedAdmissionEvent);
APPTRAVERSE_REGISTER(SharedNodeLocalState);
APPTRAVERSE_REGISTER(AddSharedPeerStateEvent);
APPTRAVERSE_REGISTER(NoteSharedLocalEventEvent);
APPTRAVERSE_REGISTER(RememberSharedLocalFrameEvent);
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
  (void)&g_apptraverse_registrar_InstallSharedAdmissionEvent;
  (void)&g_apptraverse_registrar_ConfirmSharedAdmissionEvent;
  (void)&g_apptraverse_registrar_SharedNodeLocalState;
  (void)&g_apptraverse_registrar_AddSharedPeerStateEvent;
  (void)&g_apptraverse_registrar_NoteSharedLocalEventEvent;
  (void)&g_apptraverse_registrar_RememberSharedLocalFrameEvent;
  (void)&g_apptraverse_registrar_SharedNode;
  (void)&g_apptraverse_registrar_AddSharedLinkEvent;
}

bool SharedAdmissionPlan::is_valid() const {
  return join_event_id.is_valid() && snapshot_id.is_valid() &&
         join_event_id != snapshot_id &&
         !host_endpoint.empty() && !joining_endpoint.empty() &&
         host_endpoint != joining_endpoint &&
         std::find(covered_events.begin(), covered_events.end(), join_event_id) !=
             covered_events.end() &&
         std::all_of(covered_events.begin(), covered_events.end(),
                     [](ae::ObjId id) { return id.is_valid(); });
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
         !(packet.initial && packet.coverage_notice) &&
         (!packet.coverage_notice ||
          (packet.admission.is_valid() &&
           packet.packet_id == packet.admission.snapshot_id &&
           packet.event_id == packet.admission.join_event_id)) &&
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
  auto const completed = *pending;
  pending_packets.erase(pending);
  if (completed.initial) {
    initial_complete = true;
    for (auto id : completed.covered_events) {
      AddDelivered(id);
    }
    if (admission.is_valid() && admission.snapshot_id == completed.packet_id) {
      coverage_confirmed = true;
    }
    RemoveCoveredPending(completed.covered_events);
  } else if (completed.coverage_notice) {
    if (!HasNoticeDelivered(completed.packet_id)) {
      delivered_notices.push_back(completed.packet_id);
    }
  } else {
    AddDelivered(completed.event_id);
  }
}

void SharedPeerState::RemoveCoveredPending(
    std::vector<ae::ObjId> const& covered_events) {
  pending_packets.erase(std::remove_if(pending_packets.begin(),
      pending_packets.end(), [&](auto const& packet) {
        return !packet.initial && !packet.coverage_notice &&
               std::find(covered_events.begin(), covered_events.end(),
                         packet.event_id) != covered_events.end();
      }), pending_packets.end());
}

bool SharedPeerState::HasNoticeDelivered(ae::ObjId packet_id) const {
  return std::find(delivered_notices.begin(), delivered_notices.end(),
                   packet_id) != delivered_notices.end();
}

bool SharedPeerState::IsCoveredByAdmission(ae::ObjId event_id) const {
  return admission.is_valid() &&
         std::find(admission.covered_events.begin(),
                   admission.covered_events.end(), event_id) !=
             admission.covered_events.end();
}

void SharedPeerState::InstallAdmission(SharedAdmissionPlan plan) {
  if (admission == plan && admission.is_valid()) {
    return;
  }
  auto event = InstallSharedAdmissionEvent::ptr::Create(ae::CreateWith{*domain});
  event->plan = std::move(plan);
  Commit(std::move(event));
}

bool SharedPeerState::CanApply(InstallSharedAdmissionEvent const& event) const {
  return event.plan.is_valid() && link.is_valid() && link.is_loaded() &&
         event.plan.joining_endpoint == link->endpoint_uid &&
         !admission.is_valid();
}

void SharedPeerState::Apply(InstallSharedAdmissionEvent const& event) {
  admission = event.plan;
  // Installing expected coverage is not remote delivery confirmation.
  coverage_confirmed = false;
}

void SharedPeerState::ConfirmAdmission(SharedAdmissionPlan plan) {
  if (coverage_confirmed && admission == plan) {
    return;
  }
  auto event = ConfirmSharedAdmissionEvent::ptr::Create(ae::CreateWith{*domain});
  event->plan = std::move(plan);
  Commit(std::move(event));
}

bool SharedPeerState::CanApply(ConfirmSharedAdmissionEvent const& event) const {
  return event.plan.is_valid() && admission == event.plan &&
         !coverage_confirmed;
}

void SharedPeerState::Apply(ConfirmSharedAdmissionEvent const& event) {
  coverage_confirmed = true;
  for (auto id : event.plan.covered_events) {
    AddDelivered(id);
  }
  RemoveCoveredPending(event.plan.covered_events);
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

SharedPendingPacket const* SharedNodeLocalState::FindLocalFrame(
    ae::ObjId event_id) const {
  auto const found = std::find_if(local_frames.begin(), local_frames.end(),
      [event_id](auto const& frame) { return frame.event_id == event_id; });
  return found == local_frames.end() ? nullptr : &*found;
}

void SharedNodeLocalState::RememberLocalFrame(SharedPendingPacket frame) {
  auto const* existing = FindLocalFrame(frame.event_id);
  if (existing != nullptr && *existing == frame) {
    return;
  }
  auto event = RememberSharedLocalFrameEvent::ptr::Create(ae::CreateWith{*domain});
  event->frame = std::move(frame);
  Commit(std::move(event));
}

bool SharedNodeLocalState::CanApply(
    RememberSharedLocalFrameEvent const& event) const {
  return event.frame.event_id.is_valid() && event.frame.packet_id.is_valid() &&
         !event.frame.initial && !event.frame.coverage_notice &&
         !event.frame.bytes.empty() &&
         FindLocalFrame(event.frame.event_id) == nullptr;
}

void SharedNodeLocalState::Apply(RememberSharedLocalFrameEvent const& event) {
  if (std::find(local_events.begin(), local_events.end(), event.frame.event_id) ==
      local_events.end()) {
    local_events.push_back(event.frame.event_id);
  }
  local_frames.push_back(event.frame);
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
