#include "apptraverse/shared_node.h"

#include <algorithm>
#include <limits>
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
APPTRAVERSE_REGISTER(ConfirmSharedStreamPrefixEvent);
APPTRAVERSE_REGISTER(SharedNodeLocalState);
APPTRAVERSE_REGISTER(AddSharedPeerStateEvent);
APPTRAVERSE_REGISTER(NoteSharedLocalEventEvent);
APPTRAVERSE_REGISTER(RememberSharedLocalFrameEvent);
APPTRAVERSE_REGISTER(RememberSharedStreamEvent);
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
  (void)&g_apptraverse_registrar_ConfirmSharedStreamPrefixEvent;
  (void)&g_apptraverse_registrar_SharedNodeLocalState;
  (void)&g_apptraverse_registrar_AddSharedPeerStateEvent;
  (void)&g_apptraverse_registrar_NoteSharedLocalEventEvent;
  (void)&g_apptraverse_registrar_RememberSharedLocalFrameEvent;
  (void)&g_apptraverse_registrar_RememberSharedStreamEvent;
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

bool SharedEventStamp::is_valid() const {
  return !author_uid.empty() && index != 0 && event_id.is_valid() &&
         timestamp_us != 0;
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
         (packet.stamp == SharedEventStamp{} ||
          (!packet.initial && !packet.coverage_notice &&
           packet.stamp.is_valid() && packet.stamp.event_id == packet.event_id)) &&
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

void SharedPeerState::ConfirmPrefix(SharedStreamCursor cursor) {
  if (cursor.index == 0 && cursor.timestamp_us == 0) {
    return;
  }
  auto const existing = std::find_if(acknowledged_streams.begin(),
      acknowledged_streams.end(), [&](auto const& saved) {
        return saved.author_uid == cursor.author_uid;
      });
  if (existing != acknowledged_streams.end() &&
      (existing->index > cursor.index || *existing == cursor)) {
    return;
  }
  auto event = ConfirmSharedStreamPrefixEvent::ptr::Create(ae::CreateWith{*domain});
  event->cursor = std::move(cursor);
  Commit(std::move(event));
}

bool SharedPeerState::CanApply(ConfirmSharedStreamPrefixEvent const& event) const {
  auto const& cursor = event.cursor;
  if (cursor.author_uid.empty() || cursor.index == 0 || cursor.timestamp_us == 0) {
    return false;
  }
  auto const existing = std::find_if(acknowledged_streams.begin(),
      acknowledged_streams.end(), [&](auto const& saved) {
        return saved.author_uid == cursor.author_uid;
      });
  return existing == acknowledged_streams.end() ||
         (cursor.index > existing->index &&
          cursor.timestamp_us > existing->timestamp_us);
}

void SharedPeerState::Apply(ConfirmSharedStreamPrefixEvent const& event) {
  auto const existing = std::find_if(acknowledged_streams.begin(),
      acknowledged_streams.end(), [&](auto const& saved) {
        return saved.author_uid == event.cursor.author_uid;
      });
  if (existing == acknowledged_streams.end()) {
    acknowledged_streams.push_back(event.cursor);
  } else {
    *existing = event.cursor;
  }
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
         (event.frame.stamp == SharedEventStamp{} ||
          (event.frame.stamp.event_id == event.frame.event_id &&
           CanRememberStamp(event.frame.stamp))) &&
         FindLocalFrame(event.frame.event_id) == nullptr;
}

void SharedNodeLocalState::Apply(RememberSharedLocalFrameEvent const& event) {
  if (std::find(local_events.begin(), local_events.end(), event.frame.event_id) ==
      local_events.end()) {
    local_events.push_back(event.frame.event_id);
  }
  local_frames.push_back(event.frame);
  if (event.frame.stamp.is_valid() &&
      FindStreamEvent(event.frame.stamp.event_id) == nullptr) {
    stream_events.push_back(event.frame.stamp);
  }
}

SharedEventStamp const* SharedNodeLocalState::FindStreamEvent(
    ae::ObjId event_id) const {
  auto const found = std::find_if(stream_events.begin(), stream_events.end(),
      [event_id](auto const& stamp) { return stamp.event_id == event_id; });
  return found == stream_events.end() ? nullptr : &*found;
}

SharedEventStamp const* SharedNodeLocalState::FindStreamIndex(
    std::string const& author_uid, std::uint64_t index) const {
  auto const found = std::find_if(stream_events.begin(), stream_events.end(),
      [&](auto const& stamp) {
        return stamp.author_uid == author_uid && stamp.index == index;
      });
  return found == stream_events.end() ? nullptr : &*found;
}

SharedStreamCursor SharedNodeLocalState::ReceivedPrefix(
    std::string const& author_uid) const {
  SharedStreamCursor cursor{author_uid, 0, 0};
  while (cursor.index != std::numeric_limits<std::uint64_t>::max()) {
    auto const* next = FindStreamIndex(author_uid, cursor.index + 1);
    if (next == nullptr) {
      break;
    }
    cursor.index = next->index;
    cursor.timestamp_us = next->timestamp_us;
  }
  return cursor;
}

bool SharedNodeLocalState::CanRememberStamp(SharedEventStamp const& stamp) const {
  if (!stamp.is_valid()) {
    return false;
  }
  auto const* known = FindStreamEvent(stamp.event_id);
  if (known != nullptr) {
    return *known == stamp;
  }
  for (auto const& saved : stream_events) {
    if (saved.author_uid != stamp.author_uid) {
      continue;
    }
    if (saved.index == stamp.index ||
        (saved.index < stamp.index && saved.timestamp_us >= stamp.timestamp_us) ||
        (saved.index > stamp.index && saved.timestamp_us <= stamp.timestamp_us)) {
      return false;
    }
  }
  return true;
}

void SharedNodeLocalState::RememberStreamEvent(SharedEventStamp stamp) {
  auto const* existing = FindStreamEvent(stamp.event_id);
  if (existing != nullptr && *existing == stamp) {
    return;
  }
  auto event = RememberSharedStreamEvent::ptr::Create(ae::CreateWith{*domain});
  event->stamp = std::move(stamp);
  Commit(std::move(event));
}

bool SharedNodeLocalState::CanApply(RememberSharedStreamEvent const& event) const {
  return FindStreamEvent(event.stamp.event_id) == nullptr &&
         CanRememberStamp(event.stamp);
}

void SharedNodeLocalState::Apply(RememberSharedStreamEvent const& event) {
  stream_events.push_back(event.stamp);
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
