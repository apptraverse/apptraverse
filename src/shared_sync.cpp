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

struct SharedSyncMessageV2 {
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
  SharedEventStamp stamp;
  std::vector<SharedEventStamp> stream_events;
  SharedStreamCursor received_prefix;
  AE_REFLECT_MEMBERS(major, minor, kind, acknowledged_kind, node_id, packet_id,
                    event_id, timestamp_us, graph, covered_events, admission,
                    stamp, stream_events, received_prefix)
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

SharedSyncMessageV2 LegacyIndexedMessage(SharedSyncMessage const& message) {
  return {message.major, message.minor, message.kind, message.acknowledged_kind,
          message.node_id, message.packet_id, message.event_id,
          message.timestamp_us, message.graph, message.covered_events,
          message.admission, message.stamp, message.stream_events,
          message.received_prefix};
}

SharedSyncMessage CurrentMessage(SharedSyncMessageV2 old) {
  auto message = CurrentMessage(SharedSyncMessageV1{
      old.major, old.minor, old.kind, old.acknowledged_kind, old.node_id,
      old.packet_id, old.event_id, old.timestamp_us, std::move(old.graph),
      std::move(old.covered_events), std::move(old.admission)});
  message.stamp = std::move(old.stamp);
  message.stream_events = std::move(old.stream_events);
  message.received_prefix = std::move(old.received_prefix);
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

std::vector<std::string> Members(SharedNode const& node) {
  std::vector<std::string> result;
  for (auto const& link : node.links) { result.push_back(link->endpoint_uid); }
  std::sort(result.begin(), result.end());
  return result;
}

SharedStreamCursor const* Receipt(SharedProgressMarker const& marker,
                                  std::string const& author) {
  auto const found = std::find_if(marker.received.begin(), marker.received.end(),
      [&](auto const& cursor) { return cursor.author_uid == author; });
  return found == marker.received.end() ? nullptr : &*found;
}

bool Dominates(SharedProgressMarker const& newer,
                 SharedProgressMarker const& older) {
  if (newer.members != older.members ||
      newer.through_timestamp_us < older.through_timestamp_us) { return false; }
  for (auto const& old : older.received) {
    auto const* current = Receipt(newer, old.author_uid);
    if (current == nullptr || current->index < old.index ||
        current->timestamp_us < old.timestamp_us ||
        (current->index == old.index && current->timestamp_us != old.timestamp_us)) {
      return false;
    }
  }
  return true;
}

std::vector<ae::ObjId> CoveredIds(SharedNode const& node,
    std::vector<SharedEventStamp> const& stamps, std::uint64_t checkpoint) {
  std::set<std::pair<std::uint64_t, ae::ObjId>> ordered;
  for (auto const& record : node.journal) {
    ordered.emplace(record.order.timestamp_us, record.event.id());
  }
  for (auto const& stamp : stamps) {
    if (stamp.timestamp_us <= checkpoint) {
      ordered.emplace(stamp.timestamp_us, stamp.event_id);
    }
  }
  std::vector<ae::ObjId> ids;
  for (auto const& [time, id] : ordered) { ids.push_back(id); }
  return ids;
}

// Index and time are durable author state, scoped to this SharedNode. Explicit
// time never silently changes; automatic time tolerates a clock rollback.
bool NextStamp(SharedNode const& node, std::string const& author,
               ae::ObjId event_id, std::uint64_t requested,
               SharedEventStamp& stamp) {
  std::uint64_t last_index = 0;
  auto last_time = node.local_state->timestamp_floor;
  last_time = std::max(last_time, node.local_state->checkpoint_us);
  if (auto const* progress = node.local_state->FindProgress(author)) {
    last_time = std::max(last_time, progress->through_timestamp_us);
  }
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
                             std::vector<SharedEventStamp> const& stamps,
                             std::uint64_t checkpoint) {
  if (std::any_of(node.journal.begin(), node.journal.end(), [&](auto const& r) {
        return r.order.timestamp_us <= checkpoint;
      })) { return false; }
  std::set<ae::ObjId> events;
  std::map<std::string, std::map<std::uint64_t, std::uint64_t>> streams;
  for (auto const& stamp : stamps) {
    auto const* record = FindRecord(node, stamp.event_id);
    if (!stamp.is_valid() ||
        (record == nullptr && stamp.timestamp_us > checkpoint) ||
        (record != nullptr && record->order.timestamp_us != stamp.timestamp_us) ||
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
  if (message.major != 1 || message.minor > 3 ||
      !message.node_id.is_valid() || !message.packet_id.is_valid()) {
    return false;
  }
  if ((message.kind != SharedSyncKind::Progress &&
       message.progress != SharedProgressMarker{}) ||
      (message.kind != SharedSyncKind::InitialState && message.checkpoint_us != 0) ||
      (message.minor < 3 && (message.progress != SharedProgressMarker{} ||
                             message.checkpoint_us != 0))) { return false; }
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
  if (message.minor >= 2 &&
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
               message.acknowledged_kind == SharedSyncKind::CoverageReceipt) ||
              (message.minor >= 3 &&
               message.acknowledged_kind == SharedSyncKind::Progress)) &&
             message.graph.layers.empty() && !message.graph.root_id.is_valid() &&
             message.covered_events.empty() && message.timestamp_us == 0 &&
             !HasAdmission(plan);
    case SharedSyncKind::CoverageReceipt:
      return message.minor >= 1 && HasAdmission(plan) &&
             message.packet_id == plan.snapshot_id &&
             message.event_id == plan.join_event_id &&
             message.graph.layers.empty() && !message.graph.root_id.is_valid() &&
             message.covered_events.empty() && message.timestamp_us == 0;
    case SharedSyncKind::Progress:
      return message.minor >= 3 && message.progress.is_valid() &&
             message.packet_id == message.progress.marker_id &&
             message.event_id == message.packet_id && !HasAdmission(plan) &&
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
  if (message.minor < 3 &&
      (message.progress != SharedProgressMarker{} || message.checkpoint_us != 0 ||
       message.kind == SharedSyncKind::Progress ||
       (message.kind == SharedSyncKind::Ack &&
        message.acknowledged_kind == SharedSyncKind::Progress))) { return false; }
  std::vector<std::uint8_t> output;
  ae::seri::BinaryVectorBuffer buffer{output};
  ae::seri::BinaryArchive archive{buffer};
  auto const saved = message.minor == 0 ? archive.Save(LegacyMessage(message))
      : message.minor == 1 ? archive.Save(LegacyAdmissionMessage(message))
      : message.minor == 2 ? archive.Save(LegacyIndexedMessage(message))
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
      major != 1 || minor > 3) {
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
  } else if (minor == 2) {
    SharedSyncMessageV2 old;
    loaded = archive.Load(old).IsOk();
    if (loaded) { parsed = CurrentMessage(std::move(old)); }
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
  local->checkpoint_us = 0;
  local->progress_markers.clear();
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
  message.checkpoint_us = node->local_state->checkpoint_us;
  message.covered_events = CoveredIds(*node, node->local_state->stream_events,
                                      message.checkpoint_us);
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
    assert(record != nullptr);  // Closed origins are retired with the prefix.
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
  plan.covered_events = CoveredIds(*node, node->local_state->stream_events,
                                    node->local_state->checkpoint_us);
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
  initial.checkpoint_us = node->local_state->checkpoint_us;
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

bool SharedSyncRuntime::PublishProgress(ae::ObjId node_id,
                                        std::uint64_t through_timestamp_us) {
  auto node = FindNode(node_id);
  if (failed_ || !node.is_valid()) { return false; }
  SharedProgressMarker marker;
  marker.marker_id = ae::ObjId::GenerateUnique();
  marker.author_uid = local_endpoint_;
  marker.members = Members(*node);
  for (auto const& member : marker.members) {
    marker.received.push_back(node->local_state->ReceivedPrefix(member));
  }
  auto floor = std::max(node->local_state->timestamp_floor,
                        node->local_state->checkpoint_us);
  for (auto const id : node->local_state->local_events) {
    if (auto const* record = FindRecord(*node, id)) {
      floor = std::max(floor, record->order.timestamp_us);
    }
  }
  auto const own = node->local_state->ReceivedPrefix(local_endpoint_);
  floor = std::max(floor, own.timestamp_us);
  if (auto const* previous = node->local_state->FindProgress(local_endpoint_)) {
    floor = std::max(floor, previous->through_timestamp_us);
  }
  marker.through_timestamp_us = through_timestamp_us == 0
      ? std::max(SystemUtcMicros(), floor) : through_timestamp_us;
  if (marker.through_timestamp_us < floor || !marker.is_valid()) { return false; }
  SharedSyncMessage message;
  message.kind = SharedSyncKind::Progress;
  message.node_id = node_id;
  message.packet_id = marker.marker_id;
  message.event_id = marker.marker_id;
  message.progress = marker;
  SharedPendingPacket packet;
  packet.packet_id = marker.marker_id;
  packet.event_id = marker.marker_id;
  packet.progress = true;
  if (!SerializeSharedSyncMessage(message, packet.bytes)) { return false; }
  node->local_state->RememberProgress(std::move(marker));
  for (auto const& peer : node->local_state->peers) {
    // A refreshed report dominates this author's previous report. Keep one
    // outgoing obligation per recipient, without ACK-on-ACK chains.
    auto const previous = peer->pending_packets;
    for (auto const& old : previous) {
      if (old.progress) {
        peer->CompletePacket(old.packet_id);
        next_send_.erase(RetryKey{node_id, peer->link->endpoint_uid, old.packet_id});
      }
    }
    peer->QueuePacket(packet);
  }
  return PersistState();
}

bool SharedSyncRuntime::CompactConfirmed(ae::ObjId node_id) {
  auto node = FindNode(node_id);
  if (failed_ || !node.is_valid()) { return false; }
  auto const members = Members(*node);
  if (members.empty() || std::adjacent_find(members.begin(), members.end()) !=
                             members.end()) { return false; }
  auto through = std::numeric_limits<std::uint64_t>::max();
  for (auto const& member : members) {
    auto const* marker = node->local_state->FindProgress(member);
    if (marker == nullptr || marker->members != members) { return false; }
    auto const* own = Receipt(*marker, member);
    if (own == nullptr || node->local_state->ReceivedPrefix(member).index < own->index) {
      return false;
    }
    through = std::min(through, marker->through_timestamp_us);
  }
  if (through <= node->local_state->checkpoint_us) { return false; }
  for (auto const& record : node->journal) {
    if (record.order.timestamp_us <= through &&
        node->local_state->FindStreamEvent(record.event.id()) == nullptr) {
      return false;  // Legacy unstamped history has no all-author proof.
    }
  }
  for (auto const& author : members) {
    std::uint64_t required = 0;
    for (auto const& stamp : node->local_state->stream_events) {
      if (stamp.author_uid == author && stamp.timestamp_us <= through) {
        required = std::max(required, stamp.index);
      }
    }
    if (node->local_state->ReceivedPrefix(author).index < required) { return false; }
    for (auto const& member : members) {
      auto const* report = node->local_state->FindProgress(member);
      auto const* receipt = Receipt(*report, author);
      if (receipt == nullptr || receipt->index < required) { return false; }
    }
  }
  auto const before = node->local_state->checkpoint_us;
  auto const actual = node->CompactConfirmedPrefix(through);
  if (actual <= before) { return false; }
  for (auto it = next_send_.begin(); it != next_send_.end();) {
    if (std::get<0>(it->first) == node_id) {
      auto peer = FindPeer(*node, std::get<1>(it->first));
      if (!peer.is_valid() || !peer->FindPending(std::get<2>(it->first))) {
        it = next_send_.erase(it); continue;
      }
    }
    ++it;
  }
  return PersistState();
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
      case SharedSyncKind::Progress: accepted = OnProgress(source, message); break;
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
  auto const covered = message.minor >= 3
      ? CoveredIds(*candidate, message.stream_events, message.checkpoint_us)
      : JournalIds(*candidate);
  if (covered != message.covered_events ||
      !ValidateSnapshotStreams(*candidate, message.stream_events,
                                message.checkpoint_us)) {
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
  if (message.checkpoint_us != 0) {
    node->local_state->PruneHistory(message.checkpoint_us);
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
  if (message.timestamp_us <= node->local_state->checkpoint_us) {
    auto const* retired = node->local_state->FindStreamEvent(message.event_id);
    if (!sequenced || retired == nullptr || *retired != message.stamp) {
      return false;
    }
    // The immutable small stamp remains after payload retirement. Re-ACK an
    // exact retired identity without replaying it or importing old dependencies.
    if (!PersistState()) { return false; }
    SendAck(source, message);
    return true;
  }
  if (auto const* promise = node->local_state->FindProgress(source)) {
    if (message.timestamp_us <= promise->through_timestamp_us &&
        node->local_state->FindStreamEvent(message.event_id) == nullptr) {
      return false;  // An accepted marker promised no unknown earlier Event.
    }
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
    auto const* known = node->local_state->FindStreamEvent(message.event_id);
    bool const retired = known != nullptr && *known == stamp &&
                         stamp.timestamp_us <= node->local_state->checkpoint_us;
    if (stamp.author_uid != local_endpoint_ ||
        (!retired && (origin == nullptr || origin->stamp != stamp ||
                       origin->packet_id != message.packet_id)) ||
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
      if (!packet.initial && !packet.coverage_notice && !packet.progress &&
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
      : pending->progress ? SharedSyncKind::Progress
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
  auto const* retired = node.is_valid()
      ? node->local_state->FindStreamEvent(plan.join_event_id) : nullptr;
  if (!node.is_valid() || source != plan.host_endpoint ||
      !FindPeer(*node, source).is_valid() ||
      (FindRecord(*node, plan.join_event_id) == nullptr &&
       (retired == nullptr ||
        retired->timestamp_us > node->local_state->checkpoint_us))) {
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

bool SharedSyncRuntime::OnProgress(std::string const& source,
                                    SharedSyncMessage const& message) {
  auto node = FindNode(message.node_id);
  auto const& marker = message.progress;
  if (!node.is_valid() || marker.author_uid != source ||
      !FindPeer(*node, source).is_valid() || marker.members != Members(*node)) {
    return false;
  }
  for (auto const& receipt : marker.received) {
    if (node->local_state->ReceivedPrefix(receipt.author_uid).index < receipt.index) {
      return false;  // The report overtook data; retain sender's retry obligation.
    }
    if (receipt.index != 0) {
      auto const* boundary = node->local_state->FindStreamIndex(
          receipt.author_uid, receipt.index);
      if (boundary == nullptr || boundary->timestamp_us != receipt.timestamp_us) {
        return false;
      }
    }
  }
  auto const* own = Receipt(marker, source);
  assert(own != nullptr);
  for (auto const& stamp : node->local_state->stream_events) {
    if (stamp.author_uid == source && stamp.index > own->index &&
        stamp.timestamp_us <= marker.through_timestamp_us) { return false; }
  }
  auto const* previous = node->local_state->FindProgress(source);
  if (previous != nullptr && previous->members == marker.members &&
      Dominates(*previous, marker)) {
    // A stale or duplicate report cannot overwrite stronger saved receipts.
  } else {
    if (previous != nullptr &&
        (marker.through_timestamp_us < previous->through_timestamp_us ||
         (previous->members == marker.members && !Dominates(marker, *previous)))) {
      return false;
    }
    if (previous == nullptr &&
        marker.through_timestamp_us < node->local_state->checkpoint_us) { return false; }
    node->local_state->RememberProgress(marker);
  }
  if (!PersistState()) { return false; }
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
