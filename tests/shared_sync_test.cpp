#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/shared_sync.h"

namespace apptraverse::test {
namespace {

#define CHECK(condition)                                                    \
  do {                                                                      \
    if (!(condition)) {                                                      \
      std::cerr << "CHECK failed: " #condition << " at " << __FILE__ << ':'  \
                << __LINE__ << '\n';                                        \
      std::exit(1);                                                         \
    }                                                                       \
  } while (false)

constexpr std::uint64_t kRetry = 100;

class AppendSyncTextEvent;
class SyncRoom : public NodeFor<SyncRoom, SharedNode> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::LiveSyncRoom", SyncRoom,
                           SharedNode, 0)
 protected:
  SyncRoom() { living.insert(this); }
 public:
  explicit SyncRoom(ae::ObjProp prop) : NodeFor{prop} { living.insert(this); }
  ~SyncRoom() { living.erase(this); }
  inline static std::set<SyncRoom*> living;
  AE_OBJECT_REFLECT(AE_MMBR(text))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) { dnv(base_, text); }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const { dnv(base_, text); }
  void Apply(AppendSyncTextEvent const& event);
  void InsertSeed(Event::ptr event, std::uint64_t timestamp) {
    InsertEvent({.event = std::move(event),
                 .order = {.timestamp_us = timestamp},
                 .retained_since_us = timestamp});
  }
  std::string text;
};

class AppendSyncTextEvent : public EventFor<SyncRoom, AppendSyncTextEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::AppendLiveSyncText",
                           AppendSyncTextEvent, Event, 0)
 protected:
  AppendSyncTextEvent() = default;
 public:
  explicit AppendSyncTextEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(text), AE_MMBR(room_reference))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) { dnv(base_, text, room_reference); }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, text, room_reference);
  }
  std::string text;
  // The embedded native graph may carry a stale copy of this known Node.
  // Event import must reuse its identity, not overwrite the live replica.
  SharedNode::ptr room_reference;
};

void SyncRoom::Apply(AppendSyncTextEvent const& event) { text += event.text; }

class AttachSyncRoomEvent;
class ExpectSyncRoomEvent;
struct ExpectedSyncRoom {
  ae::ObjId id;
  std::string source;
  AE_REFLECT_MEMBERS(id, source)
};

class SyncApplication : public NodeFor<SyncApplication> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::LiveSyncApplication",
                           SyncApplication, Node, 0)
 protected:
  SyncApplication() { living.insert(this); }
 public:
  explicit SyncApplication(ae::ObjProp prop) : NodeFor{prop} { living.insert(this); }
  ~SyncApplication() { living.erase(this); }
  inline static std::set<SyncApplication*> living;
  AE_OBJECT_REFLECT(AE_MMBR(rooms), AE_MMBR(expected_rooms))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) { dnv(base_, rooms, expected_rooms); }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, rooms, expected_rooms);
  }
  void Attach(SharedNode::ptr room);
  void Expect(ae::ObjId id, std::string source);
  void Apply(AttachSyncRoomEvent const& event);
  void Apply(ExpectSyncRoomEvent const& event);
  std::vector<SharedNode::ptr> rooms;
  std::vector<ExpectedSyncRoom> expected_rooms;
};

class AttachSyncRoomEvent
    : public EventFor<SyncApplication, AttachSyncRoomEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::AttachLiveSyncRoom",
                           AttachSyncRoomEvent, Event, 0)
 protected:
  AttachSyncRoomEvent() = default;
 public:
  explicit AttachSyncRoomEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(room))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) { dnv(base_, room); }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const { dnv(base_, room); }
  SharedNode::ptr room;
};

class ExpectSyncRoomEvent
    : public EventFor<SyncApplication, ExpectSyncRoomEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::ExpectLiveSyncRoom",
                           ExpectSyncRoomEvent, Event, 0)
 protected:
  ExpectSyncRoomEvent() = default;
 public:
  explicit ExpectSyncRoomEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(expected))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) { dnv(base_, expected); }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const { dnv(base_, expected); }
  ExpectedSyncRoom expected;
};

void SyncApplication::Attach(SharedNode::ptr room) {
  auto event = AttachSyncRoomEvent::ptr::Create(ae::CreateWith{*domain});
  event->room = std::move(room);
  Commit(std::move(event));
}
void SyncApplication::Apply(AttachSyncRoomEvent const& event) {
  rooms.push_back(event.room);
  expected_rooms.erase(std::remove_if(expected_rooms.begin(), expected_rooms.end(),
      [&](auto const& expected) { return expected.id == event.room.id(); }),
      expected_rooms.end());
}

void SyncApplication::Expect(ae::ObjId id, std::string source) {
  auto const existing = std::find_if(expected_rooms.begin(), expected_rooms.end(),
      [id](auto const& expected) { return expected.id == id; });
  CHECK(existing == expected_rooms.end());
  auto event = ExpectSyncRoomEvent::ptr::Create(ae::CreateWith{*domain});
  event->expected = {id, std::move(source)};
  Commit(std::move(event));
}

void SyncApplication::Apply(ExpectSyncRoomEvent const& event) {
  expected_rooms.push_back(event.expected);
}

APPTRAVERSE_REGISTER(SyncRoom);
APPTRAVERSE_REGISTER(AppendSyncTextEvent);
APPTRAVERSE_REGISTER(SyncApplication);
APPTRAVERSE_REGISTER(AttachSyncRoomEvent);
APPTRAVERSE_REGISTER(ExpectSyncRoomEvent);

struct Durable {
  ae::RamDomainStorage storage;
  ae::ObjId app_id;
  std::size_t saves{0};
};

struct Sent {
  std::string source;
  std::string destination;
  SharedSyncMessage message;
  std::vector<std::uint8_t> bytes;
};

class CountingLink final : public IByteLink {
 public:
  using Observer = std::function<void(std::string const&,
      std::string const&, SharedSyncMessage const&,
      std::vector<std::uint8_t> const&)>;
  CountingLink(MemoryLink& link, Observer observer)
      : link_{link}, observer_{std::move(observer)} {}
  void SetReceiver(Receiver receiver) override {
    link_.SetReceiver(std::move(receiver));
  }
  bool Send(std::string const& destination,
            std::vector<std::uint8_t> const& bytes) override {
    if (send_budget && *send_budget == 0) { return false; }
    SharedSyncMessage message;
    bool const parsed = DeserializeSharedSyncMessage(bytes, message);
    CHECK(parsed);
    observer_(link_.EndpointId(), destination, message, bytes);
    bool const accepted = link_.Send(destination, bytes);
    if (accepted && send_budget) { --*send_budget; }
    return accepted;
  }
  LinkAvailability Availability(std::string const& endpoint) const override {
    return link_.Availability(endpoint);
  }
  // Models a transport admitting only a bounded number of whole messages.
  // A budget refusal consumes neither capacity nor an outgoing-frame ledger slot.
  std::optional<std::size_t> send_budget;
 private:
  MemoryLink& link_;
  Observer observer_;
};

struct Replica {
  Durable& durable;
  ae::RamDomainStorage storage;
  std::unique_ptr<ae::Domain> domain;
  SyncApplication::ptr app;
  std::unique_ptr<MemoryLink> endpoint;
  std::unique_ptr<CountingLink> transport;
  std::unique_ptr<SharedSyncRuntime> runtime;

  Replica(MemoryLinkNetwork& network, std::string uid, Durable& disk,
          CountingLink::Observer observer) : durable{disk} {
    storage = durable.storage;
    domain = std::make_unique<ae::Domain>(storage);
    if (durable.app_id.is_valid()) {
      ae::DomainGraph graph{domain.get()};
      auto held = graph.LoadRoot(durable.app_id);
      CHECK(held && held.as<SyncApplication>());
      app = SyncApplication::ptr{domain.get(), durable.app_id, {}, held};
    } else {
      app = SyncApplication::ptr::Create(ae::CreateWith{*domain});
      app->rooms.clear();
      app->expected_rooms.clear();
      InitializeRuntimeNode(*app);
      bool const saved = Persist();
      CHECK(saved);
    }
    endpoint = std::make_unique<MemoryLink>(network, uid);
    transport = std::make_unique<CountingLink>(*endpoint, std::move(observer));
    runtime = std::make_unique<SharedSyncRuntime>(
        *domain, storage, *transport, uid, [this] { return Persist(); }, kRetry);
    for (auto const& room : app->rooms) { runtime->RegisterNode(room); }
    for (auto const& expected : app->expected_rooms) {
      BindExpectation(expected);
    }
  }

  ~Replica() {
    auto const saves_before = durable.saves;
    auto* const app_address = &*app;
    std::vector<SyncRoom*> old_rooms;
    for (auto const& room : app->rooms) {
      auto* const address = domain->Find(room.id()).as<SyncRoom>();
      CHECK(address);
      old_rooms.push_back(address);
    }
    runtime.reset();
    transport.reset();
    endpoint.reset();
    app.Reset();
    domain.reset();
    CHECK(durable.saves == saves_before);
    CHECK(SyncApplication::living.count(app_address) == 0);
    for (auto* const address : old_rooms) {
      CHECK(SyncRoom::living.count(address) == 0);
    }
  }

  bool Persist() {
    ae::DomainGraph graph{domain.get()};
    auto const result = graph.SaveRoot(domain->Find(app.id()), app.id());
    if (result.IsErr()) { return false; }
    durable.storage = storage;
    durable.app_id = app.id();
    ++durable.saves;
    return true;
  }

  SyncRoom& Room(ae::ObjId id) const {
    auto room = runtime->FindNode(id);
    CHECK(room.is_valid());
    auto* typed = domain->Find(room.id()).as<SyncRoom>();
    CHECK(typed);
    return *typed;
  }

  SharedPeerState& Peer(ae::ObjId id, std::string const& uid) const {
    auto& room = Room(id);
    for (auto const& peer : room.local_state->peers) {
      if (peer->link->endpoint_uid == uid) { return *peer; }
    }
    CHECK(false);
    std::abort();
  }

  ae::ObjId Append(ae::ObjId room_id, std::string text,
                   std::uint64_t timestamp) {
    auto event = AppendSyncTextEvent::ptr::Create(ae::CreateWith{*domain});
    event->text = std::move(text);
    event->room_reference = runtime->FindNode(room_id);
    auto const id = event.id();
    bool const committed = runtime->CommitLocal(room_id, event, timestamp);
    CHECK(committed && !runtime->failed());
    return id;
  }

  void Expect(ae::ObjId id, std::string source) {
    app->Expect(id, std::move(source));
    bool const saved = Persist();
    CHECK(saved);
    BindExpectation(app->expected_rooms.back());
  }

  void BindExpectation(ExpectedSyncRoom const& expected) {
    runtime->ExpectInitialState(expected.id, expected.source, SyncRoom::kClassId,
        [this](SharedNode::ptr room) { app->Attach(std::move(room)); });
  }
};

struct ObservedEvent {
  ae::ObjId id;
  std::uint64_t timestamp;
  std::string text;
};

std::vector<ObservedEvent> Observe(ae::Domain& domain, SyncRoom const& room) {
  std::vector<ObservedEvent> events;
  std::string materialized = "room:";
  for (auto const& record : room.journal) {
    auto* event = domain.Find(record.event.id()).as<AppendSyncTextEvent>();
    CHECK(event);
    events.push_back({record.event.id(), record.order.timestamp_us, event->text});
    materialized += event->text;
  }
  CHECK(room.text == materialized);
  return events;
}

struct Pair {
  MemoryLinkNetwork network;
  Durable disk_h, disk_b;
  std::vector<Sent> sent;
  std::unique_ptr<Replica> h, b;
  std::size_t checked_acks{0};

  Pair() { RestartH(); RestartB(); }

  CountingLink::Observer Observer() {
    return [this](auto const& source, auto const& destination,
                  auto const& message, auto const& bytes) {
      if (message.kind == SharedSyncKind::Ack) {
        CheckAck(source, destination, message);
      }
      sent.push_back({source, destination, message, bytes});
    };
  }

  void RestartH() {
    h.reset();  // No Save at destruction.
    h = std::make_unique<Replica>(network, "H", disk_h, Observer());
  }
  void RestartB() {
    b.reset();
    b = std::make_unique<Replica>(network, "B", disk_b, Observer());
  }

  ae::ObjId AddRoom() {
    auto room = SyncRoom::ptr::Create(ae::CreateWith{*h->domain});
    room->text = "room:";
    for (auto const& uid : {"H", "B"}) {
      auto link = SharedLinkDescriptor::ptr::Create(ae::CreateWith{*h->domain});
      link->endpoint_uid = uid;
      InitializeRuntimeNode(*link);
      room->links.push_back(link);
    }
    SharedSyncRuntime::InitializeLocalState(*room);
    InitializeRuntimeNode(*room);
    h->app->Attach(room);
    h->runtime->RegisterNode(room);
    bool const persisted = h->Persist();
    CHECK(persisted);
    b->Expect(room.id(), "H");
    return room.id();
  }

  void Bootstrap(ae::ObjId id) {
    bool const queued = h->runtime->QueueInitialState(id, "B");
    CHECK(queued);
    h->runtime->Service(network.NowUs());
    Drain();
    CHECK(h->Peer(id, "B").initial_complete);
    CHECK(b->app->rooms.size() == h->app->rooms.size());
    CHECK(!h->runtime->failed() && !b->runtime->failed());
  }

  void Drain(bool reverse = false) {
    for (unsigned turns = 0; turns != 1000; ++turns) {
      auto packets = network.PendingPackets();
      if (packets.empty()) { return; }
      auto const packet = reverse ? packets.back() : packets.front();
      bool const delivered = network.Deliver(packet.id);
      CHECK(delivered);
    }
    CHECK(false);
  }

  SharedSyncMessage Decode(MemoryLinkNetwork::Packet const& packet) const {
    SharedSyncMessage result;
    bool const parsed = DeserializeSharedSyncMessage(packet.bytes, result);
    CHECK(parsed);
    return result;
  }

  MemoryLinkNetwork::Packet Packet(SharedSyncKind kind,
                                   std::string const& source,
                                   ae::ObjId event_id = {}) const {
    for (auto const& packet : network.PendingPackets()) {
      auto const message = Decode(packet);
      if (message.kind == kind && packet.source == source &&
          (!event_id.is_valid() || message.event_id == event_id)) {
        return packet;
      }
    }
    CHECK(false);
    return {};
  }

  void Service(std::uint64_t now) {
    network.AdvanceTo(now);
    h->runtime->Service(now);
    b->runtime->Service(now);
    CHECK(!h->runtime->failed() && !b->runtime->failed());
  }

  void Equal(ae::ObjId id, std::size_t count) const {
    auto const left = Observe(*h->domain, h->Room(id));
    auto const right = Observe(*b->domain, b->Room(id));
    CHECK(left.size() == count && right.size() == count);
    CHECK(h->Room(id).text == b->Room(id).text);
    for (std::size_t i = 0; i != count; ++i) {
      CHECK(left[i].id == right[i].id);
      CHECK(left[i].timestamp == right[i].timestamp);
      CHECK(left[i].text == right[i].text);
    }
  }

  void CheckAck(std::string const& source, std::string const& destination,
                SharedSyncMessage const& ack) {
    auto original = std::find_if(sent.begin(), sent.end(), [&](auto const& frame) {
      return frame.source == destination && frame.destination == source &&
             frame.message.node_id == ack.node_id &&
             frame.message.packet_id == ack.packet_id &&
             frame.message.kind == ack.acknowledged_kind;
    });
    CHECK(original != sent.end());
    auto const& disk = source == "H" ? disk_h : disk_b;
    CHECK(disk.app_id.is_valid());
    // Read from the durable snapshot through the Application's saved edge,
    // never from the live receiver or an incidental stored orphan object.
    auto durable = disk.storage;
    ae::Domain domain{durable};
    ae::DomainGraph graph{&domain};
    auto held = graph.LoadRoot(disk.app_id);
    CHECK(held);
    auto* app = held.as<SyncApplication>();
    CHECK(app);
    SyncRoom* room = nullptr;
    for (auto const& item : app->rooms) {
      if (item.id() == ack.node_id) {
        room = domain.Find(item.id()).as<SyncRoom>();
      }
    }
    CHECK(room);
    if (ack.minor == 2 && ack.acknowledged_kind == SharedSyncKind::Event) {
      auto const prefix = room->local_state->ReceivedPrefix(destination);
      CHECK(ack.received_prefix == prefix && ack.stamp == original->message.stamp);
      for (std::uint64_t index = 1; index <= prefix.index; ++index) {
        auto const* stamp = room->local_state->FindStreamIndex(destination, index);
        CHECK(stamp && stamp->timestamp_us <= prefix.timestamp_us);
        auto const record = std::find_if(room->journal.begin(), room->journal.end(),
            [&](auto const& value) { return value.event.id() == stamp->event_id; });
        CHECK(record != room->journal.end() && record->order.timestamp_us == stamp->timestamp_us);
      }
    }
    auto const actual = Observe(domain, *room);
    auto expected_storage = ae::RamDomainStorage{};
    expected_storage.state = original->message.graph.layers;
    ae::Domain expected_domain{expected_storage};
    ae::DomainGraph expected_graph{
        &expected_domain, ae::GraphSerializationScope::NetworkShared};
    auto expected_root = expected_graph.LoadRoot(original->message.graph.root_id);
    CHECK(expected_root);
    std::vector<ObservedEvent> expected;
    if (original->message.kind == SharedSyncKind::InitialState) {
      auto* initial_room = expected_root.as<SyncRoom>();
      CHECK(initial_room);
      expected = Observe(expected_domain, *initial_room);
    } else {
      auto* event = expected_root.as<AppendSyncTextEvent>();
      CHECK(event);
      expected.push_back({original->message.event_id,
                          original->message.timestamp_us, event->text});
    }
    for (auto const& item : expected) {
      auto const found = std::find_if(actual.begin(), actual.end(),
          [&](auto const& observed) { return observed.id == item.id; });
      CHECK(found != actual.end());
      CHECK(found->timestamp == item.timestamp && found->text == item.text);
    }
    ++checked_acks;
  }
};

void TestInitialStateWinsSingleMessageBackpressure() {
  Pair pair;
  auto const id = pair.AddRoom();
  auto const seed = pair.h->Append(id, "|H10", 10);
  bool const queued = pair.h->runtime->QueueInitialState(id, "B");
  CHECK(queued);
  auto const& pending = pair.h->Peer(id, "B").pending_packets;
  CHECK(pending.size() == 2 && !pending.front().initial && pending.back().initial);
  auto const frozen_initial = pending.back();
  CHECK(pair.b->app->rooms.empty());
  pair.h->transport->send_budget = 1;
  pair.Service(0);
  auto const wire = pair.network.PendingPackets();
  CHECK(wire.size() == 1);
  auto const message = pair.Decode(wire.front());
  CHECK(message.kind == SharedSyncKind::InitialState);
  CHECK(wire.front().bytes == frozen_initial.bytes);
  CHECK(message.covered_events == std::vector<ae::ObjId>{seed});
  CHECK(pair.sent.size() == 1 && pair.sent.front().message.kind == SharedSyncKind::InitialState);
  pair.Drain();
  pair.Equal(id, 1);
  CHECK(pair.h->Peer(id, "B").initial_complete);
  CHECK(pair.h->Peer(id, "B").HasDelivered(seed));
  CHECK(pair.h->Peer(id, "B").pending_packets.empty());
  pair.h->transport->send_budget = 1;
  pair.Service(kRetry);
  CHECK(pair.network.PendingPackets().empty());
  // Once initial coverage is acknowledged, the same constrained transport
  // delivers a later Event normally without replaying the covered seed.
  auto const later = pair.h->Append(id, "|H20", 20);
  pair.Service(kRetry);
  auto const next = pair.Packet(SharedSyncKind::Event, "H", later);
  CHECK(pair.Decode(next).event_id == later);
  pair.Drain();
  pair.Equal(id, 2);
  CHECK(pair.h->Peer(id, "B").pending_packets.empty());
  CHECK(pair.checked_acks >= 2);
}

void TestInitialLossFrozenCoverageAndLateEvent() {
  Pair pair;
  auto const id = pair.AddRoom();
  auto seed = AppendSyncTextEvent::ptr::Create(ae::CreateWith{*pair.h->domain});
  seed->text = "seed";
  seed->room_reference = pair.h->runtime->FindNode(id);
  pair.h->Room(id).InsertSeed(seed, 10);
  bool queued = pair.h->runtime->QueueInitialState(id, "B");
  CHECK(queued);
  pair.Service(0);
  auto first = pair.Packet(SharedSyncKind::InitialState, "H");
  bool dropped = pair.network.Drop(first.id);
  CHECK(dropped);
  auto const later = pair.h->Append(id, "|late", 20);
  pair.Service(kRetry - 1);
  auto const early_event = pair.Packet(SharedSyncKind::Event, "H", later);
  bool delivered = pair.network.Deliver(early_event.id);
  CHECK(delivered);
  CHECK(pair.b->app->rooms.empty());
  CHECK(pair.network.PendingPackets().empty());  // No premature Event ACK.
  pair.Service(kRetry);
  auto retry = pair.Packet(SharedSyncKind::InitialState, "H");
  CHECK(retry.bytes == first.bytes);
  CHECK(pair.Decode(retry).packet_id == pair.Decode(first).packet_id);
  delivered = pair.network.Deliver(retry.id);
  CHECK(delivered);
  auto const ack = pair.Packet(SharedSyncKind::Ack, "B");
  dropped = pair.network.Drop(ack.id);
  CHECK(dropped);
  CHECK(pair.b->Room(id).text == "room:seed");
  pair.Service(2 * kRetry);
  pair.Drain(true);
  pair.Service(3 * kRetry);
  pair.Drain();
  pair.Equal(id, 2);
  CHECK(pair.h->Peer(id, "B").HasDelivered(seed.id()));
  CHECK(pair.h->Peer(id, "B").HasDelivered(later));
  CHECK(pair.h->Peer(id, "B").pending_packets.empty());
  CHECK(pair.checked_acks >= 3);
}

void TestInitialRestartBeforeSendAndAfterApply() {
  Pair pair;
  auto const id = pair.AddRoom();
  bool const queued = pair.h->runtime->QueueInitialState(id, "B");
  CHECK(queued);
  auto const frozen = pair.h->Peer(id, "B").pending_packets.front();
  CHECK(frozen.initial);
  CHECK(pair.network.PendingPackets().empty());
  CHECK(pair.b->app->expected_rooms.size() == 1);
  pair.RestartH();  // Pending was saved before the first Send.
  pair.RestartB();  // Bootstrap config is local Application state.
  CHECK(pair.b->app->rooms.empty());
  CHECK(pair.b->app->expected_rooms.size() == 1);
  CHECK(pair.b->app->expected_rooms[0].id == id);
  CHECK(pair.b->app->expected_rooms[0].source == "H");
  pair.Service(0);
  auto const initial = pair.Packet(SharedSyncKind::InitialState, "H");
  CHECK(initial.bytes == frozen.bytes);
  CHECK(pair.Decode(initial).packet_id == frozen.packet_id);
  bool delivered = pair.network.Deliver(initial.id);
  CHECK(delivered);
  CHECK(pair.b->app->expected_rooms.empty());
  CHECK(pair.b->app->rooms.size() == 1);
  auto const ack = pair.Packet(SharedSyncKind::Ack, "B");
  bool const dropped = pair.network.Drop(ack.id);
  CHECK(dropped);
  pair.RestartB();  // Initial root + receipt survived; the ACK did not arrive.
  CHECK(pair.b->app->expected_rooms.empty());
  CHECK(pair.b->app->rooms.size() == 1);
  CHECK(pair.b->Peer(id, "H").HasReceivedInitial(frozen.packet_id));
  pair.Service(kRetry);
  auto const retry = pair.Packet(SharedSyncKind::InitialState, "H");
  CHECK(retry.bytes == frozen.bytes);
  pair.Drain();
  CHECK(pair.h->Peer(id, "B").initial_complete);
  CHECK(pair.h->Peer(id, "B").pending_packets.empty());
  CHECK(pair.b->app->rooms.size() == 1);
  pair.Equal(id, 0);
}

void TestLiveEventLossDuplicateAndReorderedBothDirections() {
  Pair pair;
  auto const id = pair.AddRoom();
  pair.Bootstrap(id);
  auto const e20 = pair.h->Append(id, "|20", 20);
  pair.Service(0);
  auto first = pair.Packet(SharedSyncKind::Event, "H", e20);
  bool dropped = pair.network.Drop(first.id);
  CHECK(dropped);
  auto const e10 = pair.b->Append(id, "|10", 10);
  auto const e30 = pair.h->Append(id, "|30", 30);
  pair.Service(kRetry - 1);
  auto duplicate_packet = pair.Packet(SharedSyncKind::Event, "H", e30);
  auto const duplicate = pair.network.Duplicate(duplicate_packet.id);
  CHECK(duplicate != 0);
  pair.Drain(true);
  CHECK(pair.h->Peer(id, "B").FindPending(pair.Decode(first).packet_id));
  pair.Service(kRetry);
  auto retry = pair.Packet(SharedSyncKind::Event, "H", e20);
  CHECK(retry.bytes == first.bytes);
  pair.Drain(true);
  pair.Equal(id, 3);
  CHECK(pair.h->Room(id).text == "room:|10|20|30");
  CHECK(pair.h->Peer(id, "B").HasDelivered(e30));
  CHECK(pair.h->Peer(id, "B").HasDelivered(e20));
  CHECK(pair.b->Peer(id, "H").HasDelivered(e10));
}

void TestLostAckAndReceiverRestart() {
  Pair pair;
  auto const id = pair.AddRoom();
  pair.Bootstrap(id);
  auto const event = pair.h->Append(id, "|once", 100);
  CHECK(pair.h->transport->Availability("B") == LinkAvailability::Unknown);
  CHECK(pair.b->transport->Availability("H") == LinkAvailability::Unknown);
  pair.Service(0);
  auto const data = pair.Packet(SharedSyncKind::Event, "H", event);
  auto const first_sends = pair.sent.size();
  bool const lost_event = pair.network.Drop(data.id);
  CHECK(lost_event);
  pair.Service(kRetry - 1);
  CHECK(pair.sent.size() == first_sends);
  pair.Service(kRetry);
  CHECK(pair.sent.size() == first_sends + 1);
  auto const after_loss = pair.Packet(SharedSyncKind::Event, "H", event);
  CHECK(after_loss.bytes == data.bytes);
  bool delivered = pair.network.Deliver(after_loss.id);
  CHECK(delivered);
  auto const ack = pair.Packet(SharedSyncKind::Ack, "B");
  bool dropped = pair.network.Drop(ack.id);
  CHECK(dropped);
  CHECK(pair.h->Peer(id, "B").pending_packets.size() == 1);
  pair.RestartB();  // Apply/persist survived; the ACK did not arrive.
  pair.Equal(id, 1);
  auto const before_ack_retry = pair.sent.size();
  pair.Service(2 * kRetry - 1);
  CHECK(pair.sent.size() == before_ack_retry);
  CHECK(pair.h->Peer(id, "B").pending_packets.size() == 1);
  pair.Service(2 * kRetry);
  CHECK(pair.sent.size() == before_ack_retry + 1);
  auto const retried = pair.Packet(SharedSyncKind::Event, "H", event);
  CHECK(retried.bytes == data.bytes);
  auto const duplicate = pair.network.Duplicate(retried.id);
  CHECK(duplicate != 0);
  pair.Drain();
  pair.Equal(id, 1);
  CHECK(pair.h->Peer(id, "B").pending_packets.empty());
  auto const sends = pair.sent.size();
  pair.RestartH();  // Saved ACK must not become pending again.
  pair.Service(3 * kRetry);
  CHECK(pair.sent.size() == sends);
  pair.Equal(id, 1);
  auto const next = pair.h->Append(id, "|after-restart", 200);
  CHECK(pair.h->Peer(id, "B").pending_packets.size() == 1);
  pair.Service(3 * kRetry);
  CHECK(pair.sent.size() == sends + 1);
  CHECK(pair.Packet(SharedSyncKind::Event, "H", next).bytes ==
        pair.h->Peer(id, "B").pending_packets.front().bytes);
  pair.Drain();
  pair.Equal(id, 2);
}

void TestPendingBeforeSendAndBothSidesRestart() {
  Pair pair;
  auto const id = pair.AddRoom();
  pair.Bootstrap(id);
  auto const h_event = pair.h->Append(id, "|H", 100);
  auto const b_event = pair.b->Append(id, "|B", 200);
  CHECK(pair.network.PendingPackets().empty());
  auto const frozen_h = pair.h->Peer(id, "B").pending_packets.front();
  auto const frozen_b = pair.b->Peer(id, "H").pending_packets.front();
  pair.RestartH();
  pair.RestartB();
  CHECK(pair.h->Peer(id, "B").pending_packets.front().bytes == frozen_h.bytes);
  CHECK(pair.b->Peer(id, "H").pending_packets.front().bytes == frozen_b.bytes);
  pair.Service(0);
  CHECK(pair.Packet(SharedSyncKind::Event, "H", h_event).bytes == frozen_h.bytes);
  CHECK(pair.Packet(SharedSyncKind::Event, "B", b_event).bytes == frozen_b.bytes);
  pair.Drain(true);
  pair.Equal(id, 2);
  CHECK(pair.h->Peer(id, "B").pending_packets.empty());
  CHECK(pair.b->Peer(id, "H").pending_packets.empty());
}

void TestWrongAckOfflinePacingAndNodeIsolation() {
  Pair pair;
  auto const first_id = pair.AddRoom();
  pair.Bootstrap(first_id);
  auto const second_id = pair.AddRoom();
  pair.Bootstrap(second_id);
  auto const event1 = pair.h->Append(first_id, "|one", 100);
  auto const event2 = pair.h->Append(second_id, "|two", 200);
  pair.Service(0);
  auto const data1 = pair.Packet(SharedSyncKind::Event, "H", event1);
  auto const data2 = pair.Packet(SharedSyncKind::Event, "H", event2);
  auto const message = pair.Decode(data1);
  SharedSyncMessage forged;
  forged.kind = SharedSyncKind::Ack;
  forged.acknowledged_kind = SharedSyncKind::Event;
  forged.node_id = message.node_id;
  forged.packet_id = message.packet_id;
  forged.event_id = message.event_id;
  forged.stamp = message.stamp;
  forged.received_prefix = {"H", message.stamp.index, message.stamp.timestamp_us};
  auto send_forged = [&](MemoryLink& endpoint, SharedSyncMessage const& ack) {
    std::vector<std::uint8_t> bytes;
    bool const serialized = SerializeSharedSyncMessage(ack, bytes);
    CHECK(serialized);
    bool const sent = endpoint.Send("H", bytes);
    CHECK(sent);
    auto const packet = pair.network.PendingPackets().back();
    bool const delivered = pair.network.Deliver(packet.id);
    CHECK(delivered);
  };
  MemoryLink stranger{pair.network, "stranger"};
  send_forged(stranger, forged);
  forged.node_id = second_id;
  send_forged(*pair.b->endpoint, forged);
  forged.node_id = first_id;
  forged.packet_id = ae::ObjId::GenerateUnique();
  send_forged(*pair.b->endpoint, forged);
  CHECK(pair.h->Peer(first_id, "B").pending_packets.size() == 1);
  CHECK(pair.h->Peer(second_id, "B").pending_packets.size() == 1);
  bool delivered = pair.network.Deliver(data1.id);
  CHECK(delivered);
  auto const ack1 = pair.Packet(SharedSyncKind::Ack, "B");
  delivered = pair.network.Deliver(ack1.id);
  CHECK(delivered);
  CHECK(pair.h->Peer(first_id, "B").pending_packets.empty());
  CHECK(pair.h->Peer(second_id, "B").pending_packets.size() == 1);
  bool dropped = pair.network.Drop(data2.id);
  CHECK(dropped);

  pair.network.SetAvailability("B", LinkAvailability::Offline);
  auto const sends = pair.sent.size();
  pair.Service(50 * kRetry);
  pair.Service(100 * kRetry);
  CHECK(pair.sent.size() == sends);
  CHECK(pair.h->Peer(second_id, "B").pending_packets.size() == 1);
  pair.network.SetAvailability("B", LinkAvailability::Online);
  pair.Service(100 * kRetry);
  auto const resumed = pair.Packet(SharedSyncKind::Event, "H", event2);
  CHECK(resumed.bytes == data2.bytes);
  auto const after_resume = pair.sent.size();
  pair.Service(100 * kRetry);
  pair.Service(101 * kRetry - 1);
  CHECK(pair.sent.size() == after_resume);
  pair.Service(101 * kRetry);
  CHECK(pair.sent.size() == after_resume + 1);
  pair.Drain(true);
  pair.Equal(first_id, 1);
  pair.Equal(second_id, 1);
}

void TestDuplicateInitialDoesNotReplaceLiveReplica() {
  Pair pair;
  auto const id = pair.AddRoom();
  bool const queued = pair.h->runtime->QueueInitialState(id, "B");
  CHECK(queued);
  pair.Service(0);
  auto const initial = pair.Packet(SharedSyncKind::InitialState, "H");
  bool delivered = pair.network.Deliver(initial.id);
  CHECK(delivered);
  auto const ack = pair.Packet(SharedSyncKind::Ack, "B");
  bool dropped = pair.network.Drop(ack.id);
  CHECK(dropped);
  auto const b_event = pair.b->Append(id, "|B-local", 100);
  pair.Service(0);
  pair.Drain();
  pair.Equal(id, 1);
  pair.RestartB();
  pair.Service(kRetry);
  auto const retry = pair.Packet(SharedSyncKind::InitialState, "H");
  CHECK(retry.bytes == initial.bytes);
  pair.Drain();
  pair.Equal(id, 1);
  CHECK(pair.b->Room(id).local_state->local_events.size() == 1);
  CHECK(pair.b->Room(id).local_state->local_events[0] == b_event);
}

void TestUnsupportedVersionDoesNotMutateOrAcknowledge() {
  Pair pair;
  auto const id = pair.AddRoom();
  pair.Bootstrap(id);
  auto const event = pair.h->Append(id, "|versioned", 100);
  pair.Service(0);
  auto const data = pair.Packet(SharedSyncKind::Event, "H", event);
  auto unsupported = pair.Decode(data);
  unsupported.major = 2;
  bool const dropped = pair.network.Drop(data.id);
  CHECK(dropped);
  std::vector<std::uint8_t> bytes;
  bool const serialized = SerializeSharedSyncMessage(unsupported, bytes);
  CHECK(serialized);
  auto const saves = pair.disk_b.saves;
  auto const sends = pair.sent.size();
  auto const rejections = pair.b->runtime->rejected_messages();
  // Bypass CountingLink's valid-message observation for this deliberate
  // unsupported frame. Its rejection must not produce a success ACK.
  bool const sent = pair.h->endpoint->Send("B", bytes);
  CHECK(sent);
  auto const packet = pair.network.PendingPackets().back();
  bool const delivered = pair.network.Deliver(packet.id);
  CHECK(delivered);
  CHECK(pair.disk_b.saves == saves);
  CHECK(pair.sent.size() == sends);
  CHECK(pair.b->runtime->rejected_messages() == rejections + 1);
  CHECK(pair.b->Room(id).journal.empty());
  CHECK(pair.b->Room(id).text == "room:");
  CHECK(pair.network.PendingPackets().empty());
  pair.Service(kRetry);
  pair.Drain();
  pair.Equal(id, 1);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureSharedNodeRegistration();
  apptraverse::test::TestInitialStateWinsSingleMessageBackpressure();
  apptraverse::test::TestInitialLossFrozenCoverageAndLateEvent();
  apptraverse::test::TestInitialRestartBeforeSendAndAfterApply();
  apptraverse::test::TestLiveEventLossDuplicateAndReorderedBothDirections();
  apptraverse::test::TestLostAckAndReceiverRestart();
  apptraverse::test::TestPendingBeforeSendAndBothSidesRestart();
  apptraverse::test::TestWrongAckOfflinePacingAndNodeIsolation();
  apptraverse::test::TestDuplicateInitialDoesNotReplaceLiveReplica();
  apptraverse::test::TestUnsupportedVersionDoesNotMutateOrAcknowledge();
  std::cout << "shared_sync_test OK\n";
}
