#include "apptraverse/shared_sync.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <utility>

#include "aether-miscpp/serialization/binary_archive.h"
#include "aether-objects/obj/registry.h"
#include "apptraverse/runtime_node.h"

namespace apptraverse {
namespace {

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

// Trial insertion is isolated: failed out-of-order replay may partially mutate
// its target. Both the current node and the incoming Event use native layers.
bool CanInsert(SharedNode const& node, SharedGraphSnapshot const& event_graph,
                std::uint64_t timestamp_us) {
  SharedGraphSnapshot current;
  if (!CaptureSharedGraph(node, current)) {
    return false;
  }
  ae::RamDomainStorage scratch;
  scratch.state = std::move(current.layers);
  ae::Domain domain{scratch};
  ae::DomainGraph graph{&domain, ae::GraphSerializationScope::NetworkShared};
  auto held = graph.LoadRoot(node.obj_id);
  auto* trial = held ? held.as<SharedNode>() : nullptr;
  if (trial == nullptr) {
    return false;
  }
  ae::Ptr<ae::Obj> imported;
  if (!ImportSharedGraphNewObjects(event_graph, domain, scratch, imported)) {
    return false;
  }
  auto* event = imported.as<Event>();
  if (event == nullptr || !IsTargetClass(*event, *trial)) {
    return false;
  }
  Event::ptr reference{&domain, event->obj_id, {}, imported};
  return trial->TryInsertJournalEvent(
      EventRecord{.event = std::move(reference),
                   .order = {.timestamp_us = timestamp_us},
                   .retained_since_us = SystemUtcMicros()});
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
  if (message.major != 1 || message.minor != 0 ||
      !message.node_id.is_valid() || !message.packet_id.is_valid()) {
    return false;
  }
  switch (message.kind) {
    case SharedSyncKind::InitialState:
      return !message.event_id.is_valid() && message.timestamp_us == 0 &&
             message.graph.root_id == message.node_id;
    case SharedSyncKind::Event:
      return message.event_id.is_valid() && message.timestamp_us != 0 &&
             message.graph.root_id == message.event_id &&
             message.covered_events.empty();
    case SharedSyncKind::Ack:
      return (message.acknowledged_kind == SharedSyncKind::InitialState ||
              message.acknowledged_kind == SharedSyncKind::Event) &&
             message.graph.layers.empty() && !message.graph.root_id.is_valid() &&
             message.covered_events.empty() && message.timestamp_us == 0;
  }
  return false;
}

}  // namespace

bool SerializeSharedSyncMessage(SharedSyncMessage const& message,
                                std::vector<std::uint8_t>& bytes) {
  std::vector<std::uint8_t> output;
  ae::seri::BinaryVectorBuffer buffer{output};
  ae::seri::BinaryArchive archive{buffer};
  if (archive.Save(message).IsErr()) {
    return false;
  }
  bytes = std::move(output);
  return true;
}

bool DeserializeSharedSyncMessage(std::vector<std::uint8_t> const& bytes,
                                  SharedSyncMessage& message) {
  auto copy = bytes;
  ae::seri::BinaryVectorBuffer buffer{copy};
  ae::seri::BinaryArchive archive{buffer};
  SharedSyncMessage parsed;
  if (archive.Load(parsed).IsErr() ||
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
  InitializeRuntimeNode(*local);
  node.local_state = std::move(local);
}

void SharedSyncRuntime::RegisterNode(SharedNode::ptr node) {
  assert(node.is_valid() && node.is_loaded() && node.domain() == &domain_ &&
         node->local_state.is_valid());
  // Membership changes in the local metadata subtree are local Events.
  for (auto const& descriptor : node->links) {
    if (descriptor->endpoint_uid == local_endpoint_ ||
        FindPeer(*node, descriptor->endpoint_uid).is_valid()) {
      continue;
    }
    auto peer = SharedPeerState::ptr::Create(ae::CreateWith{domain_});
    peer->link = descriptor;
    peer->delivered_events.clear();
    peer->pending_packets.clear();
    peer->initial_complete = false;
    peer->received_initial_packets.clear();
    InitializeRuntimeNode(*peer);
    node->local_state->AddPeer(std::move(peer));
  }
  nodes_[node.id()] = std::move(node);
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

bool SharedSyncRuntime::QueueLocalEvents(SharedNode& node,
                                          SharedGraphSnapshot const* newly_frozen) {
  for (auto const id : node.local_state->local_events) {
    auto const* record = FindRecord(node, id);
    assert(record != nullptr);  // Shared history is held until checkpoint work.
    for (auto const& peer : node.local_state->peers) {
      if (peer->HasDelivered(id)) {
        continue;
      }
      bool const queued = std::any_of(peer->pending_packets.begin(),
          peer->pending_packets.end(), [&](auto const& packet) {
            return packet.event_id == id || Includes(packet.covered_events, id);
          });
      if (queued) {
        continue;
      }
      SharedSyncMessage message;
      message.kind = SharedSyncKind::Event;
      message.node_id = node.obj_id;
      message.packet_id = id;
      message.event_id = id;
      message.timestamp_us = record->order.timestamp_us;
      if (newly_frozen != nullptr && newly_frozen->root_id == id) {
        message.graph = *newly_frozen;
      } else if (!CaptureSharedGraph(*record->event, message.graph)) {
        return false;
      }
      SharedPendingPacket pending;
      pending.packet_id = id;
      pending.event_id = id;
      if (!SerializeSharedSyncMessage(message, pending.bytes)) {
        return false;
      }
      peer->QueuePacket(std::move(pending));
    }
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
  if (timestamp_us == 0) {
    timestamp_us = SystemUtcMicros();
    if (!node->journal.empty() &&
        timestamp_us <= node->journal.back().order.timestamp_us) {
      timestamp_us = node->journal.back().order.timestamp_us + 1;
    }
  }
  SharedGraphSnapshot frozen;
  if (!CaptureSharedGraph(*event, frozen) ||
      !CanInsert(*node, frozen, timestamp_us)) {
    return false;
  }
  auto const id = event.id();
  InsertValidated(*node, std::move(event), timestamp_us);
  node->local_state->NoteLocalEvent(id);
  // The payload is the immutable pre-Apply closure. A newly learned mutable
  // dependency must not arrive with post-Event state and be changed twice.
  return QueueLocalEvents(*node, &frozen) && PersistState();
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
  auto* candidate = root ? root.as<SharedNode>() : nullptr;
  if (candidate == nullptr || JournalIds(*candidate) != message.covered_events) {
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
    // A known journal entry may have been created by local model work since
    // the last save. Every success ACK still answers durable state.
    if (!PersistState()) {
      return false;
    }
    SendAck(source, message);
    return true;
  }
  if (!CanInsert(*node, message.graph, message.timestamp_us)) {
    return false;
  }
  ae::Ptr<ae::Obj> imported;
  if (!ImportSharedGraphNewObjects(message.graph, domain_, storage_, imported)) {
    return false;
  }
  Event::ptr event{&domain_, message.event_id, {}, imported};
  InsertValidated(*node, std::move(event), message.timestamp_us);
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
  auto const* pending = peer->FindPending(message.packet_id);
  if (pending == nullptr) {
    return true;  // Delayed duplicate ACK has no state transition.
  }
  auto const expected_kind = pending->initial ? SharedSyncKind::InitialState
                                             : SharedSyncKind::Event;
  if (message.acknowledged_kind != expected_kind ||
      message.event_id != pending->event_id) {
    return false;
  }
  peer->CompletePacket(message.packet_id);
  next_send_.erase(RetryKey{message.node_id, source, message.packet_id});
  return PersistState();
}

void SharedSyncRuntime::SendAck(std::string const& destination,
                                SharedSyncMessage const& original) {
  if (failed_ || link_.Availability(destination) == LinkAvailability::Offline) {
    return;
  }
  SharedSyncMessage ack;
  ack.kind = SharedSyncKind::Ack;
  ack.acknowledged_kind = original.kind;
  ack.node_id = original.node_id;
  ack.packet_id = original.packet_id;
  ack.event_id = original.event_id;
  std::vector<std::uint8_t> bytes;
  if (SerializeSharedSyncMessage(ack, bytes)) {
    link_.Send(destination, bytes);
  }
}

}  // namespace apptraverse
