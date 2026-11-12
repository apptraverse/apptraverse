#include "apptraverse/shared_sync.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <set>
#include <utility>

#include "aether-miscpp/serialization/binary_archive.h"
#include "aether-objects/obj/registry.h"
#include "apptraverse/runtime_node.h"

namespace apptraverse {
namespace {

// The original 1.0 schema remains a native reflected type so persisted frozen
// packets can still be decoded without changing their identity or bytes.
struct SharedSyncMessageV0 {
  std::uint16_t major;
  std::uint16_t minor;
  SharedSyncKind kind;
  SharedSyncKind acknowledged_kind;
  ae::ObjId node_id;
  ae::ObjId packet_id;
  ae::ObjId event_id;
  std::uint64_t timestamp_us;
  SharedGraphSnapshot graph;
  std::vector<ae::ObjId> covered_events;
  AE_REFLECT_MEMBERS(major, minor, kind, acknowledged_kind, node_id, packet_id,
                    event_id, timestamp_us, graph, covered_events)
};

struct SharedSyncMessageV1 {
  std::uint16_t major;
  std::uint16_t minor;
  SharedSyncKind kind;
  SharedSyncKind acknowledged_kind;
  ae::ObjId node_id;
  ae::ObjId packet_id;
  ae::ObjId event_id;
  std::uint64_t timestamp_us;
  SharedGraphSnapshot graph;
  std::vector<ae::ObjId> covered_events;
  SharedAdmissionPlan admission;
  AE_REFLECT_MEMBERS(major, minor, kind, acknowledged_kind, node_id, packet_id,
                    event_id, timestamp_us, graph, covered_events, admission)
};

bool HasAdmission(SharedAdmissionPlan const& plan) {
  return plan.join_event_id.is_valid();
}

bool ValidAdmission(SharedAdmissionPlan const& plan) {
  if (!plan.is_valid()) {
    return false;
  }
  std::set<ae::ObjId> distinct;
  for (auto const id : plan.covered_events) {
    if (!id.is_valid() || !distinct.insert(id).second) {
      return false;
    }
  }
  return distinct.count(plan.join_event_id) != 0;
}

SharedSyncMessageV0 LegacyMessage(SharedSyncMessage const& message) {
  return {message.major, message.minor, message.kind, message.acknowledged_kind,
          message.node_id, message.packet_id, message.event_id,
          message.timestamp_us, message.graph, message.covered_events};
}

SharedSyncMessage CurrentMessage(SharedSyncMessageV0 old) {
  SharedSyncMessage message;
  message.major = old.major;
  message.minor = old.minor;
  message.kind = old.kind;
  message.acknowledged_kind = old.acknowledged_kind;
  message.node_id = old.node_id;
  message.packet_id = old.packet_id;
  message.event_id = old.event_id;
  message.timestamp_us = old.timestamp_us;
  message.graph = std::move(old.graph);
  message.covered_events = std::move(old.covered_events);
  return message;
}

SharedSyncMessageV1 LegacyAdmissionMessage(SharedSyncMessage const& message) {
  return {message.major, message.minor, message.kind, message.acknowledged_kind,
          message.node_id, message.packet_id, message.event_id,
          message.timestamp_us, message.graph, message.covered_events,
          message.admission};
}

SharedSyncMessage CurrentMessage(SharedSyncMessageV1 old) {
  auto message = CurrentMessage(SharedSyncMessageV0{
      old.major, old.minor, old.kind, old.acknowledged_kind, old.node_id,
      old.packet_id, old.event_id, old.timestamp_us, std::move(old.graph),
      std::move(old.covered_events)});
  message.admission = std::move(old.admission);
  return message;
}

bool Includes(std::vector<ae::ObjId> const& ids, ae::ObjId id) {
  return std::find(ids.begin(), ids.end(), id) != ids.end();
}

bool IsTargetClass(Event const& event, SharedNode const& node) {
  return ae::Registry::GetRegistry().GenerationDistance(
             event.TargetClassId(), node.GetClassId()) >= 0;
}

EventRecord const* FindRecord(SharedNode const& node, ae::ObjId id) {
  auto const found = std::find_if(node.journal.begin(), node.journal.end(),
      [id](auto const& record) { return record.event.id() == id; });
  return found == node.journal.end() ? nullptr : &*found;
}

std::vector<ae::ObjId> JournalIds(SharedNode const& node) {
  std::vector<ae::ObjId> ids;
  for (auto const& record : node.journal) {
    ids.push_back(record.event.id());
  }
  return ids;
}

// Index and time are durable author state, scoped to this SharedNode. Explicit
// time never silently changes; automatic time tolerates a clock rollback.
bool NextStamp(SharedNode const& node, std::string const& author,
               ae::ObjId event_id, std::uint64_t requested,
               SharedEventStamp& stamp) {
  std::uint64_t last_index = 0;
  auto last_time = node.local_state->timestamp_floor;
  for (auto const& known : node.local_state->stream_events) {
    if (known.author_uid == author) {
      last_index = std::max(last_index, known.index);
      last_time = std::max(last_time, known.timestamp_us);
    }
  }
  // Legacy local origins have no invented indexes, but their time still binds
  // the upgraded author's chronological generation contract.
  for (auto const id : node.local_state->local_events) {
    if (auto const* record = FindRecord(node, id)) {
      last_time = std::max(last_time, record->order.timestamp_us);
    }
  }
  auto const max = std::numeric_limits<std::uint64_t>::max();
  if (last_index == max || last_time == max) {
    return false;
  }
  auto time = requested;
  if (time == 0) {
    auto floor = last_time;
    if (!node.journal.empty()) {
      floor = std::max(floor, node.journal.back().order.timestamp_us);
    }
    if (floor == max) {
      return false;
    }
    time = std::max(SystemUtcMicros(), floor + 1);
  }
  if (time <= last_time) {
    return false;
  }
  stamp = {author, last_index + 1, event_id, time};
  return true;
}

bool ValidateSnapshotStreams(SharedNode const& node,
                             std::vector<SharedEventStamp> const& stamps) {
  std::set<ae::ObjId> events;
  std::map<std::string, std::map<std::uint64_t, std::uint64_t>> streams;
  for (auto const& stamp : stamps) {
    auto const* record = FindRecord(node, stamp.event_id);
    if (!stamp.is_valid() || record == nullptr ||
        record->order.timestamp_us != stamp.timestamp_us ||
        !events.insert(stamp.event_id).second ||
        !std::any_of(node.links.begin(), node.links.end(), [&](auto const& link) {
          return link->endpoint_uid == stamp.author_uid;
        }) ||
        !streams[stamp.author_uid].emplace(stamp.index, stamp.timestamp_us).second) {
      return false;
    }
  }
  for (auto const& [author, stream] : streams) {
    std::uint64_t previous = 0;
    for (auto const& [index, time] : stream) {
      if (time <= previous) {
        return false;
      }
      previous = time;
    }
  }
  return true;
}

// Trial insertion is isolated: failed out-of-order replay may partially mutate
// its target. Both the current node and the incoming Event use native layers.
bool CanInsert(SharedNode const& node, SharedGraphSnapshot const& event_graph,
                std::uint64_t timestamp_us, std::string const& joining_endpoint = {}) {
  SharedGraphSnapshot current;
  if (!CaptureSharedGraph(node, current)) {
    return false;
  }
  ae::RamDomainStorage scratch;
  scratch.state = std::move(current.layers);
  ae::Domain domain{scratch};
  ae::DomainGraph graph{&domain, ae::GraphSerializationScope::NetworkShared};
  auto held = graph.LoadRoot(node.obj_id);
  if (!held || ae::Registry::GetRegistry().GenerationDistance(
                   SharedNode::kClassId, held->GetClassId()) < 0) {
    return false;
  }
  auto* trial = held.as<SharedNode>();
  ae::Ptr<ae::Obj> imported;
  if (!ImportSharedGraphNewObjects(event_graph, domain, scratch, imported)) {
    return false;
  }
  if (ae::Registry::GetRegistry().GenerationDistance(
          Event::kClassId, imported->GetClassId()) < 0) {
    return false;
  }
  auto* event = imported.as<Event>();
  if (!IsTargetClass(*event, *trial)) {
    return false;
  }
  Event::ptr reference{&domain, event->obj_id, {}, imported};
  bool const inserted = trial->TryInsertJournalEvent(
      EventRecord{.event = std::move(reference),
                   .order = {.timestamp_us = timestamp_us},
                   .retained_since_us = SystemUtcMicros()});
  if (!inserted) {
    return false;
  }
  return joining_endpoint.empty() ||
         std::count_if(trial->links.begin(), trial->links.end(),
             [&](auto const& link) {
               return link->endpoint_uid == joining_endpoint;
             }) == 1;
}

void InsertValidated(SharedNode& node, Event::ptr event,
                      std::uint64_t timestamp_us) {
  bool const applied = node.TryInsertJournalEvent(
      EventRecord{.event = std::move(event),
                   .order = {.timestamp_us = timestamp_us},
                   .retained_since_us = SystemUtcMicros()});
  if (!applied) {
    std::fputs("Validated SharedNode insertion failed\n", stderr);
    std::abort();
  }
}

bool ValidMessageShape(SharedSyncMessage const& message) {
  if (message.major != 1 || message.minor > 2 ||
      !message.node_id.is_valid() || !message.packet_id.is_valid()) {
    return false;
  }
  bool const empty_stamp = message.stamp == SharedEventStamp{};
  bool const empty_prefix = message.received_prefix == SharedStreamCursor{};
  if (message.minor < 2 && (!empty_stamp || !empty_prefix ||
                            !message.stream_events.empty())) {
    return false;
  }
  if (message.kind != SharedSyncKind::InitialState &&
      !message.stream_events.empty()) {
    return false;
  }
  bool const event_ack = message.kind == SharedSyncKind::Ack &&
                        message.acknowledged_kind == SharedSyncKind::Event;
  if (message.minor == 2 &&
      (message.kind == SharedSyncKind::Event || event_ack)) {
    if (!message.stamp.is_valid() ||
        message.stamp.event_id != message.event_id ||
        message.packet_id != message.event_id) {
      return false;
    }
    if (event_ack) {
      auto const& prefix = message.received_prefix;
      if (prefix.author_uid != message.stamp.author_uid ||
          ((prefix.index == 0) != (prefix.timestamp_us == 0))) {
        return false;
      }
    } else if (!empty_prefix ||
               message.stamp.timestamp_us != message.timestamp_us) {
      return false;
    }
  } else if (!empty_stamp || !empty_prefix) {
    return false;
  }
  auto const& plan = message.admission;
  if (HasAdmission(plan)) {
    if (message.minor == 0 || !ValidAdmission(plan)) {
      return false;
    }
  } else if (plan.snapshot_id.is_valid() || !plan.host_endpoint.empty() ||
             !plan.joining_endpoint.empty() || !plan.covered_events.empty()) {
    return false;
  }
  switch (message.kind) {
    case SharedSyncKind::InitialState:
      return !message.event_id.is_valid() && message.timestamp_us == 0 &&
             message.graph.root_id == message.node_id &&
             (!HasAdmission(plan) ||
              (plan.snapshot_id == message.packet_id &&
               plan.covered_events == message.covered_events));
    case SharedSyncKind::Event:
      return message.event_id.is_valid() && message.timestamp_us != 0 &&
             message.graph.root_id == message.event_id &&
             message.covered_events.empty() &&
             (!HasAdmission(plan) || plan.join_event_id == message.event_id);
    case SharedSyncKind::Ack:
      return (message.acknowledged_kind == SharedSyncKind::InitialState ||
              message.acknowledged_kind == SharedSyncKind::Event ||
              (message.minor >= 1 &&
               message.acknowledged_kind == SharedSyncKind::CoverageReceipt)) &&
             message.graph.layers.empty() && !message.graph.root_id.is_valid() &&
             message.covered_events.empty() && message.timestamp_us == 0 &&
             !HasAdmission(plan);
    case SharedSyncKind::CoverageReceipt:
      return message.minor >= 1 && HasAdmission(plan) &&
             message.packet_id == plan.snapshot_id &&
             message.event_id == plan.join_event_id &&
             message.graph.layers.empty() && !message.graph.root_id.is_valid() &&
             message.covered_events.empty() && message.timestamp_us == 0;
  }
  return false;
}

}  // namespace

bool SerializeSharedSyncMessage(SharedSyncMessage const& message,
                                std::vector<std::uint8_t>& bytes) {
  // The old reflected tuple cannot carry admission metadata or its control
  // messages. Reject a lossy downgrade rather than silently dropping that state.
  if (message.minor == 0 &&
      (message.admission != SharedAdmissionPlan{} ||
       message.kind == SharedSyncKind::CoverageReceipt ||
       (message.kind == SharedSyncKind::Ack &&
        message.acknowledged_kind == SharedSyncKind::CoverageReceipt))) {
    return false;
  }
  if (message.minor < 2 &&
      (message.stamp != SharedEventStamp{} || !message.stream_events.empty() ||
       message.received_prefix != SharedStreamCursor{})) {
    return false;
  }
  std::vector<std::uint8_t> output;
  ae::seri::BinaryVectorBuffer buffer{output};
  ae::seri::BinaryArchive archive{buffer};
  auto const saved = message.minor == 0 ? archive.Save(LegacyMessage(message))
      : message.minor == 1 ? archive.Save(LegacyAdmissionMessage(message))
                           : archive.Save(message);
  if (saved.IsErr()) {
    return false;
  }
  bytes = std::move(output);
  return true;
}

bool DeserializeSharedSyncMessage(std::vector<std::uint8_t> const& bytes,
                                  SharedSyncMessage& message) {
  auto copy = bytes;
  ae::seri::BinaryVectorBuffer prefix_buffer{copy};
  ae::seri::BinaryArchive prefix{prefix_buffer};
  std::uint16_t major = 0;
  std::uint16_t minor = 0;
  if (prefix.Load(major).IsErr() || prefix.Load(minor).IsErr() ||
      major != 1 || minor > 2) {
    return false;
  }
  ae::seri::BinaryVectorBuffer buffer{copy};
  ae::seri::BinaryArchive archive{buffer};
  SharedSyncMessage parsed;
  bool loaded = false;
  if (minor == 0) {
    SharedSyncMessageV0 old;
    loaded = archive.Load(old).IsOk();
    if (loaded) {
      parsed = CurrentMessage(std::move(old));
    }
  } else if (minor == 1) {
    SharedSyncMessageV1 old;
    loaded = archive.Load(old).IsOk();
    if (loaded) {
      parsed = CurrentMessage(std::move(old));
    }
  } else {
    loaded = archive.Load(parsed).IsOk();
  }
  if (!loaded ||
      archive.buffer().read_position() != copy.size() ||
      !ValidMessageShape(parsed)) {
    return false;
  }
  message = std::move(parsed);
  return true;
}

SharedSyncRuntime::SharedSyncRuntime(ae::Domain& domain,
    ae::IDomainStorage& storage, IByteLink& link, std::string local_endpoint,
    Persist persist, std::uint64_t retry_interval_us)
    : domain_{domain}, storage_{storage}, link_{link},
      local_endpoint_{std::move(local_endpoint)}, persist_{std::move(persist)},
      retry_interval_us_{retry_interval_us} {
  assert(!local_endpoint_.empty() && persist_ && retry_interval_us_ != 0);
  link_.SetReceiver([this](auto const& source, auto const& bytes) {
    OnBytes(source, bytes);
  });
}

SharedSyncRuntime::~SharedSyncRuntime() { link_.SetReceiver({}); }

void SharedSyncRuntime::InitializeLocalState(SharedNode& node) {
  assert(!node.local_state.is_valid() && node.domain != nullptr);
  auto local = SharedNodeLocalState::ptr::Create(ae::CreateWith{*node.domain});
  local->peers.clear();
  local->local_events.clear();
  local->local_frames.clear();
  local->stream_events.clear();
  local->timestamp_floor = node.journal.empty()
      ? 0 : node.journal.back().order.timestamp_us;
  InitializeRuntimeNode(*local);
  node.local_state = std::move(local);
}

void SharedSyncRuntime::RegisterNode(SharedNode::ptr node) {
  assert(node.is_valid() && node.is_loaded() && node.domain() == &domain_ &&
         node->local_state.is_valid());
  ReconcilePeers(*node);
  nodes_[node.id()] = std::move(node);
}

void SharedSyncRuntime::ReconcilePeers(SharedNode& node) {
  // Membership changes in the local metadata subtree are local Events.
  for (auto const& descriptor : node.links) {
    if (descriptor->endpoint_uid == local_endpoint_ ||
        FindPeer(node, descriptor->endpoint_uid).is_valid()) {
      continue;
    }
    auto peer = SharedPeerState::ptr::Create(ae::CreateWith{domain_});
    peer->link = descriptor;
    peer->delivered_events.clear();
    peer->pending_packets.clear();
    peer->initial_complete = false;
    peer->received_initial_packets.clear();
    peer->admission = {};
    peer->coverage_confirmed = false;
    peer->delivered_notices.clear();
    peer->acknowledged_streams.clear();
    InitializeRuntimeNode(*peer);
    node.local_state->AddPeer(std::move(peer));
  }
}

SharedNode::ptr SharedSyncRuntime::FindNode(ae::ObjId node_id) const {
  auto const found = nodes_.find(node_id);
  return found == nodes_.end() ? SharedNode::ptr{} : found->second;
}

SharedPeerState::ptr SharedSyncRuntime::FindPeer(SharedNode const& node,
                                                std::string const& endpoint) const {
  if (!node.local_state.is_valid()) {
    return {};
  }
  for (auto const& peer : node.local_state->peers) {
    if (peer->link->endpoint_uid == endpoint) {
      return peer;
    }
  }
  return {};
}

void SharedSyncRuntime::ExpectInitialState(ae::ObjId node_id,
    std::string source_endpoint, std::uint32_t expected_class_id,
    AttachInitial attach) {
  assert(node_id.is_valid() && !source_endpoint.empty() && attach &&
         expected_class_id != 0 && !FindNode(node_id).is_valid());
  expected_[node_id] = {std::move(source_endpoint), expected_class_id,
                         std::move(attach)};
}

bool SharedSyncRuntime::PersistState() {
  if (failed_ || !persist_()) {
    failed_ = true;
    return false;
  }
  return true;
}

bool SharedSyncRuntime::QueueInitialState(ae::ObjId node_id,
                                          std::string const& destination) {
  auto node = FindNode(node_id);
  if (failed_ || !node.is_valid()) {
    return false;
  }
  auto peer = FindPeer(*node, destination);
  if (!peer.is_valid() || peer->initial_complete) {
    return false;
  }
  for (auto const& pending : peer->pending_packets) {
    if (pending.initial) {
      return true;  // Already frozen; never rebuild from newer state.
    }
  }
  SharedSyncMessage message;
  message.kind = SharedSyncKind::InitialState;
  message.node_id = node_id;
  message.packet_id = ae::ObjId::GenerateUnique();
  message.covered_events = JournalIds(*node);
  message.stream_events = node->local_state->stream_events;
  if (!CaptureSharedGraph(*node, message.graph)) {
    return false;
  }
  SharedPendingPacket pending;
  pending.packet_id = message.packet_id;
  pending.initial = true;
  pending.covered_events = message.covered_events;
  if (!SerializeSharedSyncMessage(message, pending.bytes)) {
    return false;
  }
  peer->QueuePacket(std::move(pending));
  return PersistState();
}

bool SharedSyncRuntime::QueueLocalEvents(SharedNode& node) {
  for (auto const id : node.local_state->local_events) {
    auto const* record = FindRecord(node, id);
    assert(record != nullptr);  // Shared history is held until checkpoint work.
    for (auto const& peer : node.local_state->peers) {
      if (peer->HasDelivered(id) || peer->IsCoveredByAdmission(id)) {
        continue;
      }
      bool const queued = std::any_of(peer->pending_packets.begin(),
          peer->pending_packets.end(), [&](auto const& packet) {
            return (!packet.coverage_notice && packet.event_id == id) ||
                   (packet.initial && Includes(packet.covered_events, id));
          });
      if (queued) {
        continue;
      }
      auto const* frame = node.local_state->FindLocalFrame(id);
      if (frame == nullptr) {
        // Old local metadata may have retained only IDs. It cannot recreate a
        // lost pre-Apply closure from newer dependency state. Never invent it.
        return false;
      }
      peer->QueuePacket(*frame);
    }
  }
  return true;
}

bool SharedSyncRuntime::InstallAdmission(SharedNode& node,
                                          SharedAdmissionPlan const& plan) {
  auto peer = FindPeer(node, plan.joining_endpoint);
  if (!peer.is_valid() || (peer->admission.is_valid() && peer->admission != plan)) {
    return false;
  }
  peer->InstallAdmission(plan);
  return true;
}

bool SharedSyncRuntime::QueueCoverageReceipts(SharedNode& node,
                                               SharedAdmissionPlan const& plan) {
  assert(plan.host_endpoint == local_endpoint_);
  for (auto const& peer : node.local_state->peers) {
    if (peer->link->endpoint_uid == plan.joining_endpoint ||
        peer->HasNoticeDelivered(plan.snapshot_id) ||
        peer->FindPending(plan.snapshot_id) != nullptr) {
      continue;
    }
    SharedSyncMessage notice;
    notice.kind = SharedSyncKind::CoverageReceipt;
    notice.node_id = node.obj_id;
    notice.packet_id = plan.snapshot_id;
    notice.event_id = plan.join_event_id;
    notice.admission = plan;
    SharedPendingPacket pending;
    pending.packet_id = plan.snapshot_id;
    pending.event_id = plan.join_event_id;
    pending.coverage_notice = true;
    pending.admission = plan;
    if (!SerializeSharedSyncMessage(notice, pending.bytes)) {
      return false;
    }
    peer->QueuePacket(std::move(pending));
  }
  return true;
}

bool SharedSyncRuntime::CommitLocal(ae::ObjId node_id, Event::ptr event,
                                    std::uint64_t timestamp_us) {
  auto node = FindNode(node_id);
  if (failed_ || !node.is_valid() || !event.is_valid() ||
      !event.is_loaded() || event.domain() != &domain_ ||
      !IsTargetClass(*event, *node) || FindRecord(*node, event.id()) != nullptr) {
    return false;
  }
  SharedEventStamp stamp;
  if (!NextStamp(*node, local_endpoint_, event.id(), timestamp_us, stamp)) {
    return false;
  }
  timestamp_us = stamp.timestamp_us;
  SharedGraphSnapshot frozen;
  if (!CaptureSharedGraph(*event, frozen) ||
      !CanInsert(*node, frozen, timestamp_us)) {
    return false;
  }
  auto const id = event.id();
  SharedSyncMessage message;
  message.node_id = node_id;
  message.packet_id = id;
  message.event_id = id;
  message.timestamp_us = timestamp_us;
  message.stamp = stamp;
  message.graph = std::move(frozen);
  SharedPendingPacket origin;
  origin.packet_id = id;
  origin.event_id = id;
  origin.stamp = stamp;
  if (!SerializeSharedSyncMessage(message, origin.bytes)) {
    return false;
  }
  InsertValidated(*node, std::move(event), timestamp_us);
  node->local_state->RememberLocalFrame(std::move(origin));
  // The payload is the immutable pre-Apply closure. A newly learned mutable
  // dependency must not arrive with post-Event state and be changed twice.
  return QueueLocalEvents(*node) && PersistState();
}

bool SharedSyncRuntime::AdmitPeer(ae::ObjId node_id, Event::ptr join_event,
                                   std::string joining_endpoint,
                                   std::uint64_t timestamp_us) {
  auto node = FindNode(node_id);
  if (failed_ || !node.is_valid() || !join_event.is_valid() ||
      !join_event.is_loaded() || join_event.domain() != &domain_ ||
      joining_endpoint.empty() || joining_endpoint == local_endpoint_ ||
      !IsTargetClass(*join_event, *node) ||
      FindRecord(*node, join_event.id()) != nullptr ||
      std::any_of(node->links.begin(), node->links.end(), [&](auto const& link) {
        return link->endpoint_uid == joining_endpoint;
      })) {
    return false;
  }
  SharedEventStamp stamp;
  if (!NextStamp(*node, local_endpoint_, join_event.id(), timestamp_us, stamp)) {
    return false;
  }
  timestamp_us = stamp.timestamp_us;
  SharedGraphSnapshot frozen_join;
  if (!CaptureSharedGraph(*join_event, frozen_join) ||
      !CanInsert(*node, frozen_join, timestamp_us, joining_endpoint)) {
    return false;
  }
  auto const join_id = join_event.id();
  InsertValidated(*node, std::move(join_event), timestamp_us);
  ReconcilePeers(*node);
  SharedAdmissionPlan plan;
  plan.join_event_id = join_id;
  do {
    plan.snapshot_id = ae::ObjId::GenerateUnique();
  } while (!plan.snapshot_id.is_valid() || plan.snapshot_id == join_id);
  plan.host_endpoint = local_endpoint_;
  plan.joining_endpoint = std::move(joining_endpoint);
  plan.covered_events = JournalIds(*node);
  if (!InstallAdmission(*node, plan)) {
    return false;
  }

  SharedSyncMessage join;
  join.node_id = node_id;
  join.packet_id = join_id;
  join.event_id = join_id;
  join.timestamp_us = timestamp_us;
  join.stamp = stamp;
  join.graph = std::move(frozen_join);
  join.admission = plan;
  SharedPendingPacket origin;
  origin.packet_id = join_id;
  origin.event_id = join_id;
  origin.admission = plan;
  origin.stamp = stamp;
  if (!SerializeSharedSyncMessage(join, origin.bytes)) {
    return false;
  }
  node->local_state->RememberLocalFrame(std::move(origin));

  SharedSyncMessage initial;
  initial.kind = SharedSyncKind::InitialState;
  initial.node_id = node_id;
  initial.packet_id = plan.snapshot_id;
  initial.covered_events = plan.covered_events;
  initial.admission = plan;
  initial.stream_events = node->local_state->stream_events;
  if (!CaptureSharedGraph(*node, initial.graph)) {
    return false;
  }
  SharedPendingPacket pending;
  pending.packet_id = plan.snapshot_id;
  pending.initial = true;
  pending.covered_events = plan.covered_events;
  pending.admission = plan;
  if (!SerializeSharedSyncMessage(initial, pending.bytes)) {
    return false;
  }
  FindPeer(*node, plan.joining_endpoint)->QueuePacket(std::move(pending));
  return QueueLocalEvents(*node) && PersistState();
}

void SharedSyncRuntime::Service(std::uint64_t now_us) {
  assert(now_us >= now_us_);
  now_us_ = now_us;
  if (failed_) {
    return;
  }
  for (auto const& [node_id, node] : nodes_) {
    for (auto const& peer : node->local_state->peers) {
      auto const endpoint = peer->link->endpoint_uid;
      if (link_.Availability(endpoint) == LinkAvailability::Offline) {
        continue;
      }
      // Copy before Send: an IByteLink implementation may synchronously ACK.
      auto const packets = peer->pending_packets;
      for (auto const& pending : packets) {
        RetryKey key{node_id, endpoint, pending.packet_id};
        auto const scheduled = next_send_.find(key);
        if (scheduled != next_send_.end() && now_us < scheduled->second) {
          continue;
        }
        auto const max = std::numeric_limits<std::uint64_t>::max();
        next_send_[key] = retry_interval_us_ > max - now_us
                             ? max : now_us + retry_interval_us_;
        link_.Send(endpoint, pending.bytes);
      }
    }
  }
}

void SharedSyncRuntime::OnBytes(std::string const& source,
                                std::vector<std::uint8_t> const& bytes) {
  if (failed_) {
    return;
  }
  SharedSyncMessage message;
  bool accepted = false;
  if (DeserializeSharedSyncMessage(bytes, message) && source != local_endpoint_) {
    switch (message.kind) {
      case SharedSyncKind::InitialState: accepted = OnInitial(source, message); break;
      case SharedSyncKind::Event: accepted = OnEvent(source, message); break;
      case SharedSyncKind::Ack: accepted = OnAck(source, message); break;
      case SharedSyncKind::CoverageReceipt: accepted = OnCoverage(source, message); break;
    }
  }
  if (!accepted) {
    ++rejected_messages_;
  }
}

bool SharedSyncRuntime::OnInitial(std::string const& source,
                                   SharedSyncMessage const& message) {
  auto existing = FindNode(message.node_id);
  if (existing.is_valid()) {
    auto peer = FindPeer(*existing, source);
    if (!peer.is_valid() || !peer->HasReceivedInitial(message.packet_id)) {
      return false;  // Never overwrite a live root with a second snapshot.
    }
    SendAck(source, message);
    return true;
  }
  auto const expectation = expected_.find(message.node_id);
  if (expectation == expected_.end() || expectation->second.source != source ||
      (HasAdmission(message.admission) &&
       (message.admission.host_endpoint != source ||
        message.admission.joining_endpoint != local_endpoint_)) ||
      !ValidateSharedGraphSnapshot(message.graph,
                                    expectation->second.class_id)) {
    return false;
  }
  // Check topology and exact frozen coverage in an independent native Domain.
  ae::RamDomainStorage checked;
  checked.state = message.graph.layers;
  ae::Domain checking{checked};
  ae::DomainGraph graph{&checking, ae::GraphSerializationScope::NetworkShared};
  auto root = graph.LoadRoot(message.node_id);
  if (!root || ae::Registry::GetRegistry().GenerationDistance(
                   SharedNode::kClassId, root->GetClassId()) < 0) {
    return false;
  }
  auto* candidate = root.as<SharedNode>();
  if (JournalIds(*candidate) != message.covered_events ||
      !ValidateSnapshotStreams(*candidate, message.stream_events)) {
    return false;
  }
  auto const has_endpoint = [&](std::string const& endpoint) {
    return std::any_of(candidate->links.begin(), candidate->links.end(),
        [&](auto const& descriptor) { return descriptor->endpoint_uid == endpoint; });
  };
  if (!has_endpoint(local_endpoint_) || !has_endpoint(source)) {
    return false;
  }
  // An expectation authorizes a new root, not adoption of an unrelated local
  // object with a coincident ID, even when its native class happens to match.
  if (domain_.Find(message.node_id) || !storage_.Enumerate(message.node_id).empty()) {
    return false;
  }
  ae::Ptr<ae::Obj> imported;
  if (!ImportSharedGraphNewObjects(message.graph, domain_, storage_, imported)) {
    return false;
  }
  SharedNode::ptr node{&domain_, message.node_id, {}, imported};
  InitializeLocalState(*node);
  RegisterNode(node);
  for (auto const& stamp : message.stream_events) {
    node->local_state->RememberStreamEvent(stamp);
  }
  auto peer = FindPeer(*node, source);
  peer->RememberInitial(message.packet_id, message.covered_events);
  auto attach = expectation->second.attach;
  attach(node);
  expected_.erase(message.node_id);
  if (!PersistState()) {
    return false;
  }
  SendAck(source, message);
  return true;
}

bool SharedSyncRuntime::OnEvent(std::string const& source,
                                 SharedSyncMessage const& message) {
  auto node = FindNode(message.node_id);
  // Before initial state (or a missing dependency) no success ACK is sent.
  // The sender's durable pending packet is retried after the prerequisite.
  if (!node.is_valid() || !FindPeer(*node, source).is_valid() ||
      !ValidateSharedGraphSnapshot(message.graph, Event::kClassId)) {
    return false;
  }
  bool const sequenced = message.minor >= 2;
  if (sequenced && (message.stamp.author_uid != source ||
                    !node->local_state->CanRememberStamp(message.stamp))) {
    return false;
  }
  if (!sequenced && node->local_state->FindStreamEvent(message.event_id)) {
    return false;  // Indexed data cannot silently fall back to a legacy ACK.
  }
  auto const& plan = message.admission;
  if (HasAdmission(plan)) {
    if (plan.host_endpoint != source || plan.joining_endpoint == local_endpoint_) {
      return false;
    }
    auto joining = FindPeer(*node, plan.joining_endpoint);
    if (joining.is_valid() && joining->admission.is_valid() &&
        joining->admission != plan) {
      return false;
    }
    // A newly discovered peer may need older origins after their previous
    // destination ACKed them. Require the retained pre-Apply frame, never
    // regenerate it from newer dependencies after accepting the Join.
    for (auto const id : node->local_state->local_events) {
      if (!Includes(plan.covered_events, id) &&
          node->local_state->FindLocalFrame(id) == nullptr) {
        return false;
      }
    }
  }
  auto const* known = FindRecord(*node, message.event_id);
  if (known != nullptr) {
    if (known->order.timestamp_us != message.timestamp_us) {
      return false;
    }
    SharedGraphSnapshot known_graph;
    if (!CaptureSharedGraph(*known->event, known_graph) ||
        known_graph.layers.find(message.event_id) == known_graph.layers.end() ||
        known_graph.layers.find(message.event_id)->second !=
            message.graph.layers.find(message.event_id)->second) {
      return false;  // Identity is not permission to ACK different Event data.
    }
    if (HasAdmission(plan)) {
      ReconcilePeers(*node);
      if (!InstallAdmission(*node, plan) || !QueueLocalEvents(*node)) {
        return false;
      }
    }
    if (sequenced) {
      node->local_state->RememberStreamEvent(message.stamp);
    }
    // A known journal entry may have been created by local model work since
    // the last save. Every success ACK still answers durable state.
    if (!PersistState()) {
      return false;
    }
    SendAck(source, message);
    return true;
  }
  if (!CanInsert(*node, message.graph, message.timestamp_us,
                   HasAdmission(plan) ? plan.joining_endpoint : std::string{})) {
    return false;
  }
  ae::Ptr<ae::Obj> imported;
  if (!ImportSharedGraphNewObjects(message.graph, domain_, storage_, imported)) {
    return false;
  }
  Event::ptr event{&domain_, message.event_id, {}, imported};
  InsertValidated(*node, std::move(event), message.timestamp_us);
  if (sequenced) {
    node->local_state->RememberStreamEvent(message.stamp);
  }
  if (HasAdmission(plan)) {
    // Install suppression BEFORE discovering/queueing uncovered own Events.
    ReconcilePeers(*node);
    if (!InstallAdmission(*node, plan) || !QueueLocalEvents(*node)) {
      failed_ = true;
      return false;
    }
  }
  if (!PersistState()) {
    return false;
  }
  SendAck(source, message);
  return true;
}

bool SharedSyncRuntime::OnAck(std::string const& source,
                               SharedSyncMessage const& message) {
  auto node = FindNode(message.node_id);
  if (!node.is_valid()) {
    return false;
  }
  auto peer = FindPeer(*node, source);
  if (!peer.is_valid()) {
    return false;
  }
  if (message.minor >= 2 &&
      message.acknowledged_kind == SharedSyncKind::Event) {
    auto const& stamp = message.stamp;
    auto const& prefix = message.received_prefix;
    auto const* origin = node->local_state->FindLocalFrame(message.event_id);
    if (stamp.author_uid != local_endpoint_ || origin == nullptr ||
        origin->stamp != stamp || origin->packet_id != message.packet_id ||
        prefix.author_uid != local_endpoint_) {
      return false;
    }
    if (prefix.index == 0) {
      return true;  // Durable sparse receipt, not confirmation past a hole.
    }
    auto const* boundary = node->local_state->FindStreamIndex(
        local_endpoint_, prefix.index);
    if (boundary == nullptr || boundary->timestamp_us != prefix.timestamp_us) {
      return false;
    }
    // Copy before completing: each local completion removes its pending slot.
    auto const pending = peer->pending_packets;
    for (auto const& packet : pending) {
      if (!packet.initial && !packet.coverage_notice &&
          packet.stamp.is_valid() &&
          packet.stamp.author_uid == local_endpoint_ &&
          packet.stamp.index <= prefix.index) {
        peer->CompletePacket(packet.packet_id);
        next_send_.erase(RetryKey{message.node_id, source, packet.packet_id});
      }
    }
    peer->ConfirmPrefix(prefix);
    return PersistState();
  }
  auto const* pending = peer->FindPending(message.packet_id);
  if (pending == nullptr) {
    return true;  // Delayed duplicate ACK has no state transition.
  }
  auto const expected_kind = pending->initial ? SharedSyncKind::InitialState
      : pending->coverage_notice ? SharedSyncKind::CoverageReceipt
                                 : SharedSyncKind::Event;
  if (message.acknowledged_kind != expected_kind ||
      message.event_id != pending->event_id ||
      (expected_kind == SharedSyncKind::Event && pending->stamp.is_valid())) {
    return false;
  }
  auto const admission = pending->admission;
  bool const initial_admission = pending->initial && HasAdmission(admission);
  peer->CompletePacket(message.packet_id);
  next_send_.erase(RetryKey{message.node_id, source, message.packet_id});
  // Completion and reliable dissemination form one persistent transition. A
  // crash after this save restores both the confirmed B state and notices.
  if (initial_admission && !QueueCoverageReceipts(*node, admission)) {
    failed_ = true;
    return false;
  }
  return PersistState();
}

bool SharedSyncRuntime::OnCoverage(std::string const& source,
                                    SharedSyncMessage const& message) {
  auto node = FindNode(message.node_id);
  auto const& plan = message.admission;
  if (!node.is_valid() || source != plan.host_endpoint ||
      !FindPeer(*node, source).is_valid() ||
      FindRecord(*node, plan.join_event_id) == nullptr) {
    return false;
  }
  auto joining = FindPeer(*node, plan.joining_endpoint);
  if (!joining.is_valid() || joining->admission != plan) {
    return false;  // Receipt overtook Join: durable host retry will deliver it.
  }
  joining->ConfirmAdmission(plan);
  if (!PersistState()) {
    return false;
  }
  SendAck(source, message);
  return true;
}

void SharedSyncRuntime::SendAck(std::string const& destination,
                                SharedSyncMessage const& original) {
  if (failed_ || link_.Availability(destination) == LinkAvailability::Offline) {
    return;
  }
  SharedSyncMessage ack;
  ack.major = original.major;
  ack.minor = original.minor;
  ack.kind = SharedSyncKind::Ack;
  ack.acknowledged_kind = original.kind;
  ack.node_id = original.node_id;
  ack.packet_id = original.packet_id;
  ack.event_id = original.event_id;
  if (original.kind == SharedSyncKind::Event && original.minor >= 2) {
    auto node = FindNode(original.node_id);
    assert(node.is_valid());
    ack.stamp = original.stamp;
    ack.received_prefix = node->local_state->ReceivedPrefix(
        original.stamp.author_uid);
  }
  std::vector<std::uint8_t> bytes;
  if (SerializeSharedSyncMessage(ack, bytes)) {
    link_.Send(destination, bytes);
  }
}

}  // namespace apptraverse
