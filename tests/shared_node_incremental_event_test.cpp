#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj_id.h"

#include "apptraverse/event.h"
#include "apptraverse/event_for.h"
#include "apptraverse/link.h"
#include "apptraverse/memory_transport.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/shared_event_id.h"
#include "apptraverse/shared_event_order.h"
#include "apptraverse/shared_network_graph.h"
#include "apptraverse/shared_node.h"
#include "apptraverse/shared_sync_runtime.h"
#include "apptraverse/sync_frame.h"

#include "shared_node_demo_model.h"

namespace apptraverse::test {
namespace {

using apptraverse::example::shared_node::SetValueEvent;
using apptraverse::example::shared_node::SharedValueNode;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

std::string const kEndpointA = "replica-a";
std::string const kEndpointB = "replica-b";
std::string const kEndpointC = "replica-c";

class NotePayload : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::NotePayload", NotePayload,
                           ae::Obj, 0)

 protected:
  NotePayload() = default;

 public:
  explicit NotePayload(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(text))

  std::string text;
};

class SetValueWithNoteEvent;
class NoteTargetNode : public NodeFor<NoteTargetNode> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::NoteTargetNode", NoteTargetNode,
                           Node, 0)

 protected:
  NoteTargetNode() = default;

 public:
  explicit NoteTargetNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT()

  void Apply(SetValueWithNoteEvent const&) {}
};

class SetValueWithNoteEvent
    : public EventFor<NoteTargetNode, SetValueWithNoteEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::SetValueWithNoteEvent",
                           SetValueWithNoteEvent, Event, 0)

 protected:
  SetValueWithNoteEvent() = default;

 public:
  explicit SetValueWithNoteEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(value), AE_MMBR(note))

  std::int32_t value{0};
  NotePayload::ptr note;
};

class TransitionEvent;
class StateDependentNode : public NodeFor<StateDependentNode, SharedNode> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::StateDependentNode",
                           StateDependentNode, SharedNode, 1)

 protected:
  StateDependentNode() = default;

 public:
  explicit StateDependentNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(state_code))

  template <typename Dnv>
  void Load(ae::Version<1>, Dnv& dnv) {
    SharedNode::Load(ae::Version<1>{}, dnv);
    dnv(state_code);
  }

  template <typename Dnv>
  void Save(ae::Version<1>, Dnv& dnv) const {
    SharedNode::Save(ae::Version<1>{}, dnv);
    dnv(state_code);
  }

  bool CanApply(TransitionEvent const& event) const;
  void Apply(TransitionEvent const& event);

  std::int32_t state_code{0};
};

class TransitionEvent : public EventFor<StateDependentNode, TransitionEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::TransitionEvent",
                           TransitionEvent, Event, 0)

 protected:
  TransitionEvent() = default;

 public:
  explicit TransitionEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(expected_from), AE_MMBR(new_to))

  std::int32_t expected_from{0};
  std::int32_t new_to{0};
};

bool StateDependentNode::CanApply(TransitionEvent const& event) const {
  return state_code == event.expected_from;
}

void StateDependentNode::Apply(TransitionEvent const& event) {
  state_code = event.new_to;
}

APPTRAVERSE_REGISTER(NotePayload);
APPTRAVERSE_REGISTER(NoteTargetNode);
APPTRAVERSE_REGISTER(SetValueWithNoteEvent);
APPTRAVERSE_REGISTER(StateDependentNode);
APPTRAVERSE_REGISTER(TransitionEvent);

class WatchedStorage final : public ae::IDomainStorage {
 public:
  WatchedStorage(ae::IDomainStorage& inner, MemoryNetwork const& network,
                 std::string from, std::string to)
      : inner_{inner},
        network_{network},
        from_{std::move(from)},
        to_{std::move(to)} {}

  void ResetWatch() { pending_at_store_.clear(); }

  std::vector<std::size_t> const& pending_at_store() const {
    return pending_at_store_;
  }

  std::unique_ptr<ae::IDomainStorageWriter> Store(
      ae::DomainQuery const& query) override {
    pending_at_store_.push_back(network_.PendingCount(from_, to_));
    return inner_.Store(query);
  }

  ae::ClassList Enumerate(ae::ObjId const& obj_id) override {
    return inner_.Enumerate(obj_id);
  }

  ae::DomainLoad Load(ae::DomainQuery const& query) override {
    return inner_.Load(query);
  }

  void Remove(ae::ObjId const& obj_id) override { inner_.Remove(obj_id); }
  void CleanUp() override { inner_.CleanUp(); }

 private:
  ae::IDomainStorage& inner_;
  MemoryNetwork const& network_;
  std::string from_;
  std::string to_;
  std::vector<std::size_t> pending_at_store_;
};

struct Replica {
  Replica(MemoryNetwork& network, std::string endpoint_uid,
          std::string peer_endpoint_uid)
      : watched{storage, network, endpoint_uid, std::move(peer_endpoint_uid)},
        network_{network},
        endpoint_uid_{std::move(endpoint_uid)} {}

  void Start() {
    domain = std::make_unique<ae::Domain>(watched);
    transport = std::make_unique<MemoryTransport>(network_, endpoint_uid_);
    sync = std::make_unique<SharedSyncRuntime>(*domain, watched, *transport);
  }

  void Stop() {
    sync.reset();
    transport.reset();
    domain.reset();
  }

  ae::RamDomainStorage storage;
  WatchedStorage watched;
  std::unique_ptr<ae::Domain> domain;
  std::unique_ptr<MemoryTransport> transport;
  std::unique_ptr<SharedSyncRuntime> sync;

 private:
  MemoryNetwork& network_;
  std::string endpoint_uid_;
};

struct ObserverEndpoint {
  ObserverEndpoint(MemoryNetwork& network, std::string endpoint_uid)
      : transport{network, std::move(endpoint_uid)} {
    transport.BindReceive(this, &ObserverEndpoint::Thunk);
  }

  static void Thunk(void* ctx, std::string const&,
                    std::vector<std::uint8_t> const&) {
    ++static_cast<ObserverEndpoint*>(ctx)->received;
  }

  MemoryTransport transport;
  std::size_t received = 0;
};

MemoryLink::ptr MakeMemoryLink(ae::Domain& domain, ae::ObjId id,
                               std::string endpoint) {
  auto link = MemoryLink::ptr::Create(ae::CreateWith{domain}.with_id(id));
  link->endpoint_uid = std::move(endpoint);
  link->heartbeat_interval_ms = 1000;
  InitializeRuntimeNode(*link);
  return link;
}

struct SenderFixture {
  ae::ObjId node_id;
  ae::ObjId share_to_a;
  ae::ObjId share_to_b;
};

SenderFixture BuildTopology(Replica& a, ae::ObjId::Type node_id,
                            ae::ObjId::Type link_a_id,
                            ae::ObjId::Type link_b_id) {
  auto node =
      SharedValueNode::ptr::Create(ae::CreateWith{*a.domain}.with_id(node_id));
  InitializeRuntimeNode(*node);
  auto link_a = MakeMemoryLink(*a.domain, ae::ObjId{link_a_id}, kEndpointA);
  auto link_b = MakeMemoryLink(*a.domain, ae::ObjId{link_b_id}, kEndpointB);
  node->AddShare(link_a, ShareAccess::ReadWrite);
  node->AddShare(link_b, ShareAccess::ReadWrite);
  node.Save();
  link_a.Save();
  link_b.Save();
  for (auto& entry : node->link_sync_states) {
    entry.Save();
  }

  SenderFixture fixture{
      .node_id = node.id(),
      .share_to_a = node->shares[0].share_id,
      .share_to_b = node->shares[1].share_id,
  };
  a.sync->RegisterNode(node);
  return fixture;
}

void ReloadAndRegister(Replica& replica, ae::ObjId node_id) {
  auto node = SharedValueNode::ptr::Declare(
      ae::CreateWith{*replica.domain}.with_id(node_id));
  node.Load();
  CHECK(node.is_loaded());
  replica.sync->RegisterNode(node);
}

LinkSyncState::ptr SyncStateOf(SharedNode::ptr node, ae::ObjId share_id) {
  auto const index = node->FindLinkSyncIndexForShare(share_id);
  CHECK(index < node->link_sync_states.size());
  auto state = node->link_sync_states[index];
  state.Load();
  return state;
}

SharedValueNode::ptr ConcreteOf(SharedNode::ptr node) {
  SharedValueNode::ptr concrete = node;
  concrete.Load();
  CHECK(concrete.is_loaded());
  return concrete;
}

std::int32_t ValueOf(SharedNode::ptr node) { return ConcreteOf(node)->value; }

void AllStoresBeforeAnySend(WatchedStorage const& storage) {
  CHECK(!storage.pending_at_store().empty());
  for (auto const pending : storage.pending_at_store()) {
    CHECK(pending == 0);
  }
}

Event::ptr CommitSharedValue(SharedValueNode& node, std::int32_t value,
                             SharedEventId identity,
                             std::uint64_t timestamp_us) {
  auto event = SetValueEvent::ptr::Create(ae::CreateWith{*node.domain});
  event->value = value;
  Event::ptr held = event;
  node.CommitShared(std::move(event), std::move(identity),
                    SharedEventOrder{.timestamp_us = timestamp_us});
  SharedValueNode::ptr::MakeFromThis(&node).Save();
  return held;
}

void HandshakeInitial(MemoryNetwork& network, Replica& a, Replica& b,
                      SenderFixture const& fixture) {
  b.sync->ExpectInitialNode(fixture.node_id);
  a.sync->SyncInitialState(fixture.node_id, fixture.share_to_b);
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(network.DeliverNext(kEndpointB, kEndpointA));
  CHECK(SyncStateOf(a.sync->FindNode(fixture.node_id), fixture.share_to_b)
            ->GetInitialSyncPhase() == InitialSyncPhase::Complete);
}

EventRecord const* JournalByIdentity(SharedNode const& node,
                                     SharedEventId const& identity) {
  return node.FindSharedEvent(identity);
}

void TestNormalStandaloneEventAfterInitialComplete() {
  MemoryNetwork network;
  Replica a{network, kEndpointA, kEndpointB};
  Replica b{network, kEndpointB, kEndpointA};
  a.Start();
  b.Start();

  auto const fixture = BuildTopology(a, 6001, 6002, 6003);
  HandshakeInitial(network, a, b, fixture);

  auto const identity =
      SharedEventId{.origin_uid = "peer-a", .origin_sequence = 1};
  auto const a_node = ConcreteOf(a.sync->FindNode(fixture.node_id));
  auto const a_event = CommitSharedValue(*a_node, 42, identity, 1'000);
  CHECK(a_node->value == 42);

  a.watched.ResetWatch();
  a.sync->SyncNextEvent(fixture.node_id, fixture.share_to_b);
  AllStoresBeforeAnySend(a.watched);
  CHECK(network.PendingCount(kEndpointA, kEndpointB) == 1);

  auto const a_state = SyncStateOf(a_node, fixture.share_to_b);
  CHECK(a_state->HasPendingEvent());
  CHECK(a_state->pending_event_identity == identity);
  auto const frozen = a_state->pending_event_packet;
  CHECK(network.PeekNext(kEndpointA, kEndpointB) == frozen);

  EventFrame sent;
  CHECK(DecodeEventFrame(frozen, sent));
  CHECK(sent.target_node_id == fixture.node_id);
  CHECK(sent.destination_share_id == fixture.share_to_b);
  CHECK(sent.identity == identity);
  CHECK(sent.timestamp_us == 1'000);
  CHECK(sent.event_class_id == SetValueEvent::kClassId);
  CHECK(sent.packet_id == a_state->pending_event_packet_id);

  b.watched.ResetWatch();
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  AllStoresBeforeAnySend(b.watched);

  auto const b_node = ConcreteOf(b.sync->FindNode(fixture.node_id));
  CHECK(b_node->value == 42);
  auto const* b_record = JournalByIdentity(*b_node, identity);
  CHECK(b_record != nullptr);
  CHECK(b_record->order.timestamp_us == 1'000);
  CHECK(b_record->event.id() != a_event.id());
  CHECK(b_record->identity == identity);

  CHECK(network.DeliverNext(kEndpointB, kEndpointA));
  CHECK(a_state->HasDelivered(identity));
  CHECK(!a_state->HasPendingEvent());

  a.sync->SyncNextEvent(fixture.node_id, fixture.share_to_b);
  CHECK(network.PendingCount(kEndpointA, kEndpointB) == 0);
}

void TestReceiverAllocatesLocalEventObjId() {
  MemoryNetwork network;
  Replica a{network, kEndpointA, kEndpointB};
  Replica b{network, kEndpointB, kEndpointA};
  a.Start();
  b.Start();

  auto const fixture = BuildTopology(a, 6101, 6102, 6103);
  HandshakeInitial(network, a, b, fixture);

  auto const identity =
      SharedEventId{.origin_uid = "peer-a", .origin_sequence = 2};
  auto const a_node = ConcreteOf(a.sync->FindNode(fixture.node_id));
  auto const a_event = CommitSharedValue(*a_node, 8, identity, 2'000);
  auto const sender_event_id = a_event.id();

  auto occupied = MemoryLink::ptr::Create(
      ae::CreateWith{*b.domain}.with_id(sender_event_id));
  occupied->endpoint_uid = "occupied";
  occupied->heartbeat_interval_ms = 1;
  InitializeRuntimeNode(*occupied);
  occupied.Save();
  CHECK(!b.storage.Enumerate(sender_event_id).empty());

  a.sync->SyncNextEvent(fixture.node_id, fixture.share_to_b);
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(network.DeliverNext(kEndpointB, kEndpointA));

  auto const b_node = ConcreteOf(b.sync->FindNode(fixture.node_id));
  auto const* b_record = JournalByIdentity(*b_node, identity);
  CHECK(b_record != nullptr);
  CHECK(b_record->event.id() != sender_event_id);
  CHECK(b_record->identity == identity);
  CHECK(!b.storage.Enumerate(sender_event_id).empty());
  auto still = MemoryLink::ptr::Declare(
      ae::CreateWith{*b.domain}.with_id(sender_event_id));
  still.Load();
  CHECK(still.is_loaded());
  CHECK(still->endpoint_uid == "occupied");
}

void TestMidJournalTimestampReplay() {
  MemoryNetwork network;
  Replica a{network, kEndpointA, kEndpointB};
  Replica b{network, kEndpointB, kEndpointA};
  a.Start();
  b.Start();

  auto const fixture = BuildTopology(a, 6201, 6202, 6203);
  auto const a_node = ConcreteOf(a.sync->FindNode(fixture.node_id));
  auto const e1 =
      SharedEventId{.origin_uid = "z-origin", .origin_sequence = 1};
  auto const e3 =
      SharedEventId{.origin_uid = "a-origin", .origin_sequence = 1};
  auto const e2 =
      SharedEventId{.origin_uid = "m-origin", .origin_sequence = 1};
  CommitSharedValue(*a_node, 1, e1, 100);
  CommitSharedValue(*a_node, 3, e3, 300);
  HandshakeInitial(network, a, b, fixture);

  auto const b_node = ConcreteOf(b.sync->FindNode(fixture.node_id));
  CHECK(b_node->value == 3);
  CHECK(JournalByIdentity(*b_node, e1) != nullptr);
  CHECK(JournalByIdentity(*b_node, e3) != nullptr);

  auto const a_state = SyncStateOf(a_node, fixture.share_to_b);
  CHECK(a_state->HasDelivered(e1));
  CHECK(a_state->HasDelivered(e3));

  CommitSharedValue(*a_node, 2, e2, 200);
  a.sync->SyncNextEvent(fixture.node_id, fixture.share_to_b);
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(network.DeliverNext(kEndpointB, kEndpointA));

  CHECK(b_node->value == 3);
  std::vector<std::uint64_t> timestamps;
  std::vector<SharedEventId> identities;
  for (auto const& record : b_node->journal) {
    if (!record.HasSharedIdentity()) {
      continue;
    }
    timestamps.push_back(record.order.timestamp_us);
    identities.push_back(record.identity);
  }
  CHECK(timestamps.size() == 3);
  CHECK(timestamps[0] == 100);
  CHECK(timestamps[1] == 200);
  CHECK(timestamps[2] == 300);
  CHECK(identities[1] == e2);
}

void TestLostAckExactRetryAndDuplicate() {
  MemoryNetwork network;
  Replica a{network, kEndpointA, kEndpointB};
  Replica b{network, kEndpointB, kEndpointA};
  a.Start();
  b.Start();

  auto const fixture = BuildTopology(a, 6301, 6302, 6303);
  HandshakeInitial(network, a, b, fixture);

  auto const identity =
      SharedEventId{.origin_uid = "peer-a", .origin_sequence = 3};
  auto const a_node = ConcreteOf(a.sync->FindNode(fixture.node_id));
  CommitSharedValue(*a_node, 17, identity, 3'000);
  a.sync->SyncNextEvent(fixture.node_id, fixture.share_to_b);

  auto const a_state = SyncStateOf(a_node, fixture.share_to_b);
  auto const frozen = a_state->pending_event_packet;
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  auto const b_node = ConcreteOf(b.sync->FindNode(fixture.node_id));
  auto const journal_size = b_node->journal.size();
  CHECK(b_node->value == 17);

  CHECK(network.DropNext(kEndpointB, kEndpointA));
  CHECK(a_state->HasPendingEvent());
  CHECK(a_state->pending_event_packet == frozen);

  CommitSharedValue(*a_node, 99, 
                    SharedEventId{.origin_uid = "peer-a", .origin_sequence = 99},
                    9'000);
  CHECK(a_state->pending_event_packet == frozen);

  a.sync->SyncNextEvent(fixture.node_id, fixture.share_to_b);
  CHECK(network.PeekNext(kEndpointA, kEndpointB) == frozen);

  b.watched.ResetWatch();
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(b.watched.pending_at_store().empty());
  CHECK(b_node->journal.size() == journal_size);
  CHECK(b_node->value == 17);
  CHECK(network.PendingCount(kEndpointB, kEndpointA) == 1);

  CHECK(network.DeliverNext(kEndpointB, kEndpointA));
  CHECK(a_state->HasDelivered(identity));
  CHECK(!a_state->HasPendingEvent());
}

void TestSenderRestartWhilePending() {
  MemoryNetwork network;
  Replica a{network, kEndpointA, kEndpointB};
  Replica b{network, kEndpointB, kEndpointA};
  a.Start();
  b.Start();

  auto const fixture = BuildTopology(a, 6401, 6402, 6403);
  HandshakeInitial(network, a, b, fixture);

  auto const identity =
      SharedEventId{.origin_uid = "peer-a", .origin_sequence = 4};
  CommitSharedValue(*ConcreteOf(a.sync->FindNode(fixture.node_id)), 21,
                    identity, 4'000);
  a.sync->SyncNextEvent(fixture.node_id, fixture.share_to_b);

  ae::ObjId packet_id;
  std::vector<std::uint8_t> frozen;
  {
    auto const state =
        SyncStateOf(a.sync->FindNode(fixture.node_id), fixture.share_to_b);
    packet_id = state->pending_event_packet_id;
    frozen = state->pending_event_packet;
  }
  CHECK(network.DropNext(kEndpointA, kEndpointB));

  a.Stop();
  a.Start();
  ReloadAndRegister(a, fixture.node_id);

  auto const a_state =
      SyncStateOf(a.sync->FindNode(fixture.node_id), fixture.share_to_b);
  CHECK(a_state->pending_event_packet_id == packet_id);
  CHECK(a_state->pending_event_identity == identity);
  CHECK(a_state->pending_event_packet == frozen);

  a.sync->SyncNextEvent(fixture.node_id, fixture.share_to_b);
  CHECK(network.PeekNext(kEndpointA, kEndpointB) == frozen);
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(network.DeliverNext(kEndpointB, kEndpointA));
  CHECK(a_state->HasDelivered(identity));
  CHECK(ValueOf(b.sync->FindNode(fixture.node_id)) == 21);
}

void TestReceiverRestartAfterApply() {
  MemoryNetwork network;
  Replica a{network, kEndpointA, kEndpointB};
  Replica b{network, kEndpointB, kEndpointA};
  a.Start();
  b.Start();

  auto const fixture = BuildTopology(a, 6501, 6502, 6503);
  HandshakeInitial(network, a, b, fixture);

  auto const identity =
      SharedEventId{.origin_uid = "peer-a", .origin_sequence = 5};
  CommitSharedValue(*ConcreteOf(a.sync->FindNode(fixture.node_id)), 31,
                    identity, 5'000);
  a.sync->SyncNextEvent(fixture.node_id, fixture.share_to_b);
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(network.DropNext(kEndpointB, kEndpointA));

  std::size_t journal_size = 0;
  {
    auto const b_node = ConcreteOf(b.sync->FindNode(fixture.node_id));
    journal_size = b_node->journal.size();
    CHECK(JournalByIdentity(*b_node, identity) != nullptr);
  }

  b.Stop();
  b.Start();
  ReloadAndRegister(b, fixture.node_id);

  auto const b_node = ConcreteOf(b.sync->FindNode(fixture.node_id));
  CHECK(JournalByIdentity(*b_node, identity) != nullptr);
  CHECK(b_node->value == 31);

  a.sync->SyncNextEvent(fixture.node_id, fixture.share_to_b);
  b.watched.ResetWatch();
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(b.watched.pending_at_store().empty());
  CHECK(b_node->journal.size() == journal_size);
  CHECK(network.DeliverNext(kEndpointB, kEndpointA));
  CHECK(SyncStateOf(a.sync->FindNode(fixture.node_id), fixture.share_to_b)
            ->HasDelivered(identity));
}

void TestSenderRestartAfterAck() {
  MemoryNetwork network;
  Replica a{network, kEndpointA, kEndpointB};
  Replica b{network, kEndpointB, kEndpointA};
  a.Start();
  b.Start();

  auto const fixture = BuildTopology(a, 6601, 6602, 6603);
  HandshakeInitial(network, a, b, fixture);

  auto const first =
      SharedEventId{.origin_uid = "peer-a", .origin_sequence = 6};
  auto const second =
      SharedEventId{.origin_uid = "peer-a", .origin_sequence = 7};
  auto const a_node = ConcreteOf(a.sync->FindNode(fixture.node_id));
  CommitSharedValue(*a_node, 41, first, 6'000);
  a.sync->SyncNextEvent(fixture.node_id, fixture.share_to_b);
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(network.DeliverNext(kEndpointB, kEndpointA));

  a.Stop();
  a.Start();
  ReloadAndRegister(a, fixture.node_id);

  auto const a_state =
      SyncStateOf(a.sync->FindNode(fixture.node_id), fixture.share_to_b);
  CHECK(a_state->HasDelivered(first));
  CHECK(!a_state->HasPendingEvent());

  a.sync->SyncNextEvent(fixture.node_id, fixture.share_to_b);
  CHECK(network.PendingCount(kEndpointA, kEndpointB) == 0);

  CommitSharedValue(*ConcreteOf(a.sync->FindNode(fixture.node_id)), 42, second,
                    7'000);
  a.sync->SyncNextEvent(fixture.node_id, fixture.share_to_b);
  EventFrame sent;
  CHECK(DecodeEventFrame(network.PeekNext(kEndpointA, kEndpointB), sent));
  CHECK(sent.identity == second);
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(network.DeliverNext(kEndpointB, kEndpointA));
}

void TestInitialSnapshotCoverageRace() {
  MemoryNetwork network;
  Replica a{network, kEndpointA, kEndpointB};
  Replica b{network, kEndpointB, kEndpointA};
  a.Start();
  b.Start();

  auto const fixture = BuildTopology(a, 6701, 6702, 6703);
  auto const a_node = ConcreteOf(a.sync->FindNode(fixture.node_id));
  auto const e1 =
      SharedEventId{.origin_uid = "peer-a", .origin_sequence = 10};
  auto const e2 =
      SharedEventId{.origin_uid = "peer-a", .origin_sequence = 11};
  CommitSharedValue(*a_node, 1, e1, 10'000);

  b.sync->ExpectInitialNode(fixture.node_id);
  a.sync->SyncInitialState(fixture.node_id, fixture.share_to_b);
  auto const a_state = SyncStateOf(a_node, fixture.share_to_b);
  CHECK(a_state->pending_initial_covered_event_ids.size() == 1);
  CHECK(a_state->pending_initial_covered_event_ids[0] == e1);

  CommitSharedValue(*a_node, 2, e2, 11'000);
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(network.DeliverNext(kEndpointB, kEndpointA));
  CHECK(a_state->HasDelivered(e1));
  CHECK(!a_state->HasDelivered(e2));
  CHECK(ValueOf(b.sync->FindNode(fixture.node_id)) == 1);

  a.sync->SyncNextEvent(fixture.node_id, fixture.share_to_b);
  EventFrame sent;
  CHECK(DecodeEventFrame(network.PeekNext(kEndpointA, kEndpointB), sent));
  CHECK(sent.identity == e2);
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(network.DeliverNext(kEndpointB, kEndpointA));
  CHECK(ValueOf(b.sync->FindNode(fixture.node_id)) == 2);
  CHECK(a_state->HasDelivered(e2));
}

void TestWrongSourceRejected() {
  MemoryNetwork network;
  Replica a{network, kEndpointA, kEndpointB};
  Replica b{network, kEndpointB, kEndpointA};
  a.Start();
  b.Start();
  ObserverEndpoint c{network, kEndpointC};

  auto const fixture = BuildTopology(a, 6801, 6802, 6803);
  HandshakeInitial(network, a, b, fixture);

  auto const identity =
      SharedEventId{.origin_uid = "peer-a", .origin_sequence = 12};
  CommitSharedValue(*ConcreteOf(a.sync->FindNode(fixture.node_id)), 5, identity,
                    12'000);
  a.sync->SyncNextEvent(fixture.node_id, fixture.share_to_b);
  auto const packet = network.PeekNext(kEndpointA, kEndpointB);
  CHECK(network.DropNext(kEndpointA, kEndpointB));

  c.transport.Send(kEndpointB, packet);
  b.watched.ResetWatch();
  CHECK(network.DeliverNext(kEndpointC, kEndpointB));
  CHECK(b.watched.pending_at_store().empty());
  CHECK(JournalByIdentity(*ConcreteOf(b.sync->FindNode(fixture.node_id)),
                          identity) == nullptr);
  CHECK(network.PendingCount(kEndpointB, kEndpointC) == 0);
  CHECK(c.received == 0);
}

void TestReadOnlySourceRejected() {
  MemoryNetwork network;
  Replica a{network, kEndpointA, kEndpointB};
  Replica b{network, kEndpointB, kEndpointA};
  a.Start();
  b.Start();

  auto const fixture = BuildTopology(a, 6901, 6902, 6903);
  HandshakeInitial(network, a, b, fixture);

  auto const b_node = ConcreteOf(b.sync->FindNode(fixture.node_id));
  b_node->SetShareAccess(b_node->shares[0].link, ShareAccess::ReadOnly);
  b_node.Save();

  auto const identity =
      SharedEventId{.origin_uid = "peer-a", .origin_sequence = 13};
  CommitSharedValue(*ConcreteOf(a.sync->FindNode(fixture.node_id)), 6, identity,
                    13'000);
  a.sync->SyncNextEvent(fixture.node_id, fixture.share_to_b);
  b.watched.ResetWatch();
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(b.watched.pending_at_store().empty());
  CHECK(JournalByIdentity(*b_node, identity) == nullptr);
  CHECK(network.PendingCount(kEndpointB, kEndpointA) == 0);

  b_node->SetShareAccess(b_node->shares[0].link, ShareAccess::ReadWrite);
  b_node.Save();
  a.sync->SyncNextEvent(fixture.node_id, fixture.share_to_b);
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(network.DeliverNext(kEndpointB, kEndpointA));
  CHECK(JournalByIdentity(*b_node, identity) != nullptr);
  CHECK(b_node->value == 6);
}

void TestWrongDestinationRejected() {
  MemoryNetwork network;
  Replica a{network, kEndpointA, kEndpointB};
  Replica b{network, kEndpointB, kEndpointA};
  a.Start();
  b.Start();

  auto const fixture = BuildTopology(a, 7001, 7002, 7003);
  HandshakeInitial(network, a, b, fixture);

  auto const identity =
      SharedEventId{.origin_uid = "peer-a", .origin_sequence = 14};
  auto const a_node = ConcreteOf(a.sync->FindNode(fixture.node_id));
  auto const event = CommitSharedValue(*a_node, 7, identity, 14'000);
  std::vector<std::uint8_t> payload;
  CHECK(FreezeStandaloneEventPayload(*event, payload));
  a.transport->Send(kEndpointB,
                    EncodeEventFrame(EventFrame{
                        .packet_id = ae::ObjId{7099},
                        .target_node_id = fixture.node_id,
                        .destination_share_id = fixture.share_to_a,
                        .identity = identity,
                        .timestamp_us = 14'000,
                        .event_class_id = SetValueEvent::kClassId,
                        .payload = payload,
                    }));
  b.watched.ResetWatch();
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(b.watched.pending_at_store().empty());
  CHECK(JournalByIdentity(*ConcreteOf(b.sync->FindNode(fixture.node_id)),
                          identity) == nullptr);
  CHECK(network.PendingCount(kEndpointB, kEndpointA) == 0);
}

void TestMalformedEventPayloadRejected() {
  MemoryNetwork network;
  Replica a{network, kEndpointA, kEndpointB};
  Replica b{network, kEndpointB, kEndpointA};
  a.Start();
  b.Start();

  auto const fixture = BuildTopology(a, 7101, 7102, 7103);
  HandshakeInitial(network, a, b, fixture);

  auto const identity =
      SharedEventId{.origin_uid = "peer-a", .origin_sequence = 15};
  auto const a_node = ConcreteOf(a.sync->FindNode(fixture.node_id));
  auto const event = CommitSharedValue(*a_node, 8, identity, 15'000);
  std::vector<std::uint8_t> payload;
  CHECK(FreezeStandaloneEventPayload(*event, payload));
  payload.pop_back();
  a.transport->Send(kEndpointB,
                    EncodeEventFrame(EventFrame{
                        .packet_id = ae::ObjId{7199},
                        .target_node_id = fixture.node_id,
                        .destination_share_id = fixture.share_to_b,
                        .identity = identity,
                        .timestamp_us = 15'000,
                        .event_class_id = SetValueEvent::kClassId,
                        .payload = payload,
                    }));
  b.watched.ResetWatch();
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(b.watched.pending_at_store().empty());
  CHECK(JournalByIdentity(*ConcreteOf(b.sync->FindNode(fixture.node_id)),
                          identity) == nullptr);
  CHECK(network.PendingCount(kEndpointB, kEndpointA) == 0);
}

void TestWrongTargetEventClassRejected() {
  MemoryNetwork network;
  Replica a{network, kEndpointA, kEndpointB};
  Replica b{network, kEndpointB, kEndpointA};
  a.Start();
  b.Start();

  auto const fixture = BuildTopology(a, 7201, 7202, 7203);
  HandshakeInitial(network, a, b, fixture);

  auto wrong = SetLinkInitialSyncPhaseEvent::ptr::Create(
      ae::CreateWith{*a.domain});
  wrong->phase = static_cast<std::uint8_t>(InitialSyncPhase::Complete);
  std::vector<std::uint8_t> payload;
  CHECK(FreezeStandaloneEventPayload(*wrong, payload));

  auto const identity =
      SharedEventId{.origin_uid = "peer-a", .origin_sequence = 16};
  a.transport->Send(kEndpointB,
                    EncodeEventFrame(EventFrame{
                        .packet_id = ae::ObjId{7299},
                        .target_node_id = fixture.node_id,
                        .destination_share_id = fixture.share_to_b,
                        .identity = identity,
                        .timestamp_us = 16'000,
                        .event_class_id = SetLinkInitialSyncPhaseEvent::kClassId,
                        .payload = payload,
                    }));
  b.watched.ResetWatch();
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(b.watched.pending_at_store().empty());
  CHECK(JournalByIdentity(*ConcreteOf(b.sync->FindNode(fixture.node_id)),
                          identity) == nullptr);
  CHECK(network.PendingCount(kEndpointB, kEndpointA) == 0);
}

void TestConflictingDuplicateIdentityRejected() {
  MemoryNetwork network;
  Replica a{network, kEndpointA, kEndpointB};
  Replica b{network, kEndpointB, kEndpointA};
  a.Start();
  b.Start();

  auto const fixture = BuildTopology(a, 7301, 7302, 7303);
  HandshakeInitial(network, a, b, fixture);

  auto const identity =
      SharedEventId{.origin_uid = "peer-a", .origin_sequence = 17};
  auto const a_node = ConcreteOf(a.sync->FindNode(fixture.node_id));
  auto const event = CommitSharedValue(*a_node, 9, identity, 17'000);
  a.sync->SyncNextEvent(fixture.node_id, fixture.share_to_b);
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(network.DeliverNext(kEndpointB, kEndpointA));

  std::vector<std::uint8_t> payload;
  CHECK(FreezeStandaloneEventPayload(*event, payload));
  a.transport->Send(kEndpointB,
                    EncodeEventFrame(EventFrame{
                        .packet_id = ae::ObjId{7399},
                        .target_node_id = fixture.node_id,
                        .destination_share_id = fixture.share_to_b,
                        .identity = identity,
                        .timestamp_us = 99'000,
                        .event_class_id = SetValueEvent::kClassId,
                        .payload = payload,
                    }));
  auto const b_node = ConcreteOf(b.sync->FindNode(fixture.node_id));
  auto const journal_size = b_node->journal.size();
  b.watched.ResetWatch();
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(b.watched.pending_at_store().empty());
  CHECK(b_node->journal.size() == journal_size);
  CHECK(JournalByIdentity(*b_node, identity)->order.timestamp_us == 17'000);
  CHECK(network.PendingCount(kEndpointB, kEndpointA) == 0);
}

void TestReferencedObjectGraphRefused() {
  MemoryNetwork network;
  Replica a{network, kEndpointA, kEndpointB};
  Replica b{network, kEndpointB, kEndpointA};
  a.Start();
  b.Start();

  auto const fixture = BuildTopology(a, 7401, 7402, 7403);
  HandshakeInitial(network, a, b, fixture);

  auto const a_node = ConcreteOf(a.sync->FindNode(fixture.node_id));
  auto note = NotePayload::ptr::Create(ae::CreateWith{*a.domain});
  note->text = "child";
  auto event =
      SetValueWithNoteEvent::ptr::Create(ae::CreateWith{*a.domain});
  event->value = 4;
  event->note = note;
  std::vector<std::uint8_t> payload;
  CHECK(!FreezeStandaloneEventPayload(*event, payload));
  CHECK(payload.empty());
  (void)a_node;
}

void TestMalformedClassLayersInEventRejected() {
  MemoryNetwork network;
  Replica a{network, kEndpointA, kEndpointB};
  Replica b{network, kEndpointB, kEndpointA};
  a.Start();
  b.Start();

  auto const fixture = BuildTopology(a, 7501, 7502, 7503);
  HandshakeInitial(network, a, b, fixture);

  // Synthesize a RamDomainStorage payload with unrelated class layers stored under the same ObjId.
  ae::RamDomainStorage bad_storage;
  bad_storage.state[kStandaloneEventScratchId] = ae::RamDomainStorage::ClassData{
      {SetValueEvent::kClassId, {{0, std::vector<std::uint8_t>{1, 2, 3}}}},
      {NoteTargetNode::kClassId, {{0, std::vector<std::uint8_t>{4, 5, 6}}}},
  };

  auto const payload = SerializeRamDomainStorage(bad_storage);

  auto const identity =
      SharedEventId{.origin_uid = "peer-a", .origin_sequence = 20};
  a.transport->Send(kEndpointB,
                    EncodeEventFrame(EventFrame{
                        .packet_id = ae::ObjId{7599},
                        .target_node_id = fixture.node_id,
                        .destination_share_id = fixture.share_to_b,
                        .identity = identity,
                        .timestamp_us = 20'000,
                        .event_class_id = SetValueEvent::kClassId,
                        .payload = payload,
                    }));

  b.watched.ResetWatch();
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(b.watched.pending_at_store().empty());
  CHECK(JournalByIdentity(*ConcreteOf(b.sync->FindNode(fixture.node_id)),
                          identity) == nullptr);
  CHECK(network.PendingCount(kEndpointB, kEndpointA) == 0);
}

void TestHistoricalCanApplyPreflight() {
  // Verifies that:
  // 1. An event that is valid historically (at timestamp 150, when state is 0)
  //    is admitted even though the current state is 1 (where CanApply would be false).
  // 2. An event that is NOT valid historically (e.g. requires state 1, but inserted
  //    at timestamp 50 when state is 0) is REJECTED without crashing or mutating.
  MemoryNetwork network;
  Replica a{network, kEndpointA, kEndpointB};
  Replica b{network, kEndpointB, kEndpointA};
  a.Start();
  b.Start();

  // Create topology with StateDependentNode as root
  auto a_root = StateDependentNode::ptr::Create(
      ae::CreateWith{*a.domain}.with_id(ae::ObjId{7900}));
  InitializeRuntimeNode(*a_root);
  a_root->state_code = 0;
  a_root.Save();

  auto a_link_a = MakeMemoryLink(*a.domain, ae::ObjId{7901}, kEndpointA);
  auto a_link_b = MakeMemoryLink(*a.domain, ae::ObjId{7902}, kEndpointB);
  a_root->AddShare(a_link_a, ShareAccess::ReadWrite);
  a_root->AddShare(a_link_b, ShareAccess::ReadWrite);
  a_root.Save();
  a_link_a.Save();
  a_link_b.Save();
  for (auto& entry : a_root->link_sync_states) {
    entry.Save();
  }

  SenderFixture fixture{
      .node_id = a_root.id(),
      .share_to_a = a_root->shares[0].share_id,
      .share_to_b = a_root->shares[1].share_id,
  };
  a.sync->RegisterNode(a_root);
  HandshakeInitial(network, a, b, fixture);

  // Transition node at timestamp 100: 0 -> 1
  auto e1 = TransitionEvent::ptr::Create(ae::CreateWith{*a.domain});
  e1->expected_from = 0;
  e1->new_to = 1;
  a_root->CommitShared(e1,
                       SharedEventId{.origin_uid = "peer-a", .origin_sequence = 1},
                       SharedEventOrder{.timestamp_us = 100});
  a_root.Save();
  CHECK(a_root->state_code == 1);

  // Transition node at timestamp 200: 1 -> 2
  auto e2 = TransitionEvent::ptr::Create(ae::CreateWith{*a.domain});
  e2->expected_from = 1;
  e2->new_to = 2;
  a_root->CommitShared(e2,
                       SharedEventId{.origin_uid = "peer-a", .origin_sequence = 2},
                       SharedEventOrder{.timestamp_us = 200});
  a_root.Save();
  CHECK(a_root->state_code == 2);

  // Sync both events to B
  a.sync->SyncNextEvent(fixture.node_id, fixture.share_to_b);
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(network.DeliverNext(kEndpointB, kEndpointA));

  a.sync->SyncNextEvent(fixture.node_id, fixture.share_to_b);
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(network.DeliverNext(kEndpointB, kEndpointA));

  auto const b_node = static_cast<StateDependentNode*>(
      b.sync->FindNode(fixture.node_id).operator->());
  CHECK(b_node->state_code == 2);

  // Now, craft an event e_bad with timestamp 50 (inserted before e1):
  // e_bad expects state 1 -> 9. But at timestamp 50, state is 0!
  // Note that if checked against CURRENT state of b_node (which is 2), it's also invalid,
  // but even if e_bad expected 2 -> 9, at timestamp 50 state was 0, so historical replay would fail!
  auto e_bad = TransitionEvent::ptr::Create(ae::CreateWith{*a.domain});
  e_bad->expected_from = 2;  // matches CURRENT state (2), but NOT historical state at t=50 (0)!
  e_bad->new_to = 9;
  std::vector<std::uint8_t> bad_payload;
  CHECK(FreezeStandaloneEventPayload(*e_bad, bad_payload));

  auto const bad_id =
      SharedEventId{.origin_uid = "peer-a", .origin_sequence = 3};
  a.transport->Send(kEndpointB,
                    EncodeEventFrame(EventFrame{
                        .packet_id = ae::ObjId{8888},
                        .target_node_id = fixture.node_id,
                        .destination_share_id = fixture.share_to_b,
                        .identity = bad_id,
                        .timestamp_us = 50,
                        .event_class_id = TransitionEvent::kClassId,
                        .payload = bad_payload,
                    }));

  b.watched.ResetWatch();
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  // e_bad must be rejected by preflight because at t=50 state is 0, not 2.
  CHECK(b.watched.pending_at_store().empty());
  CHECK(JournalByIdentity(*b_node, bad_id) == nullptr);
  CHECK(b_node->state_code == 2);
  CHECK(network.PendingCount(kEndpointB, kEndpointA) == 0);

  // Next, craft an event e_good with timestamp 150 (between e1 and e2).
  // At t=150, state is 1!
  // e_good transitions 1 -> 3.
  // And at t=200, e2 originally expected 1 -> 2. If e_good changed state to 3, e2 would fail.
  // So e_good must transition 1 -> 1 so e2 (which expects 1 -> 2) will still succeed afterwards!
  // Notice: at the moment e_good arrives, CURRENT state is 2! e_good has expected_from = 1 != 2.
  // If e_good were checked against current state, CanApply would fail!
  // But historically at t=150, state is 1, so e_good CanApply is true!
  auto e_good = TransitionEvent::ptr::Create(ae::CreateWith{*a.domain});
  e_good->expected_from = 1;  // matches historical state at t=150! Does NOT match CURRENT state (2)!
  e_good->new_to = 1;         // leaves state as 1 so e2 (1 -> 2) at t=200 remains valid!
  std::vector<std::uint8_t> good_payload;
  CHECK(FreezeStandaloneEventPayload(*e_good, good_payload));

  auto const good_id =
      SharedEventId{.origin_uid = "peer-a", .origin_sequence = 4};
  a.transport->Send(kEndpointB,
                    EncodeEventFrame(EventFrame{
                        .packet_id = ae::ObjId{8889},
                        .target_node_id = fixture.node_id,
                        .destination_share_id = fixture.share_to_b,
                        .identity = good_id,
                        .timestamp_us = 150,
                        .event_class_id = TransitionEvent::kClassId,
                        .payload = good_payload,
                    }));

  b.watched.ResetWatch();
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  // e_good must be accepted and persisted!
  CHECK(!b.watched.pending_at_store().empty());
  CHECK(JournalByIdentity(*b_node, good_id) != nullptr);
  CHECK(b_node->state_code == 2);  // after full replay: 0 -(t=100)-> 1 -(t=150)-> 1 -(t=200)-> 2
  CHECK(network.PendingCount(kEndpointB, kEndpointA) == 1);
  CHECK(network.DeliverNext(kEndpointB, kEndpointA));
}

void TestEventFrameDecodingIsStrict() {
  std::vector<std::uint8_t> payload{1, 2, 3};
  EventFrame const frame{
      .packet_id = ae::ObjId{31},
      .target_node_id = ae::ObjId{32},
      .destination_share_id = ae::ObjId{33},
      .identity = SharedEventId{.origin_uid = "peer-a", .origin_sequence = 1},
      .timestamp_us = 100,
      .event_class_id = SetValueEvent::kClassId,
      .payload = payload,
  };
  auto const bytes = EncodeEventFrame(frame);
  EventFrame decoded;
  CHECK(DecodeEventFrame(bytes, decoded));
  CHECK(decoded.identity == frame.identity);
  CHECK(decoded.timestamp_us == 100);
  CHECK(decoded.event_class_id == SetValueEvent::kClassId);
  CHECK(decoded.payload == payload);

  auto trailing = bytes;
  trailing.push_back(0);
  CHECK(!DecodeEventFrame(trailing, decoded));

  auto truncated = bytes;
  truncated.pop_back();
  CHECK(!DecodeEventFrame(truncated, decoded));

  CHECK(!DecodeEventFrame(
      EncodeEventFrame(EventFrame{
          .packet_id = ae::ObjId{},
          .target_node_id = ae::ObjId{32},
          .destination_share_id = ae::ObjId{33},
          .identity = frame.identity,
          .timestamp_us = 100,
          .event_class_id = SetValueEvent::kClassId,
          .payload = payload,
      }),
      decoded));
  CHECK(!DecodeEventFrame(
      EncodeEventFrame(EventFrame{
          .packet_id = ae::ObjId{31},
          .target_node_id = ae::ObjId{32},
          .destination_share_id = ae::ObjId{33},
          .identity = SharedEventId{.origin_uid = "", .origin_sequence = 1},
          .timestamp_us = 100,
          .event_class_id = SetValueEvent::kClassId,
          .payload = payload,
      }),
      decoded));
  CHECK(!DecodeEventFrame(
      EncodeEventFrame(EventFrame{
          .packet_id = ae::ObjId{31},
          .target_node_id = ae::ObjId{32},
          .destination_share_id = ae::ObjId{33},
          .identity = SharedEventId{.origin_uid = "peer-a", .origin_sequence = 0},
          .timestamp_us = 100,
          .event_class_id = SetValueEvent::kClassId,
          .payload = payload,
      }),
      decoded));
  CHECK(!DecodeEventFrame(
      EncodeEventFrame(EventFrame{
          .packet_id = ae::ObjId{31},
          .target_node_id = ae::ObjId{32},
          .destination_share_id = ae::ObjId{33},
          .identity = frame.identity,
          .timestamp_us = 0,
          .event_class_id = SetValueEvent::kClassId,
          .payload = payload,
      }),
      decoded));
  CHECK(!DecodeEventFrame(
      EncodeEventFrame(EventFrame{
          .packet_id = ae::ObjId{31},
          .target_node_id = ae::ObjId{32},
          .destination_share_id = ae::ObjId{33},
          .identity = frame.identity,
          .timestamp_us = 100,
          .event_class_id = SharedValueNode::kClassId,
          .payload = payload,
      }),
      decoded));

  SyncFrameType type{};
  CHECK(PeekSyncFrameType(bytes, type));
  CHECK(type == SyncFrameType::kEvent);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::example::shared_node::EnsureSharedNodeDemoRegistration();

  apptraverse::test::TestNormalStandaloneEventAfterInitialComplete();
  apptraverse::test::TestReceiverAllocatesLocalEventObjId();
  apptraverse::test::TestMidJournalTimestampReplay();
  apptraverse::test::TestLostAckExactRetryAndDuplicate();
  apptraverse::test::TestSenderRestartWhilePending();
  apptraverse::test::TestReceiverRestartAfterApply();
  apptraverse::test::TestSenderRestartAfterAck();
  apptraverse::test::TestInitialSnapshotCoverageRace();
  apptraverse::test::TestWrongSourceRejected();
  apptraverse::test::TestReadOnlySourceRejected();
  apptraverse::test::TestWrongDestinationRejected();
  apptraverse::test::TestMalformedEventPayloadRejected();
  apptraverse::test::TestMalformedClassLayersInEventRejected();
  apptraverse::test::TestHistoricalCanApplyPreflight();
  apptraverse::test::TestWrongTargetEventClassRejected();
  apptraverse::test::TestConflictingDuplicateIdentityRejected();
  apptraverse::test::TestReferencedObjectGraphRefused();
  apptraverse::test::TestEventFrameDecodingIsStrict();

  std::cout << "shared_node_incremental_event_test OK\n";
  return 0;
}
