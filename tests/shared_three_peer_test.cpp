#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

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

class MeshParticipant : public NodeFor<MeshParticipant> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::MeshParticipant",
                           MeshParticipant, Node, 0)
 protected:
  MeshParticipant() = default;
 public:
  explicit MeshParticipant(ae::ObjProp prop) : NodeFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(name), AE_MMBR(link))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) { dnv(base_, name, link); }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const { dnv(base_, name, link); }
  std::string name;
  SharedLinkDescriptor::ptr link;
};

class MeshTextEvent;
class MeshJoinEvent;
class MeshRoom : public NodeFor<MeshRoom, SharedNode> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::MeshRoom", MeshRoom, SharedNode, 0)
 protected:
  MeshRoom() { living.insert(this); }
 public:
  explicit MeshRoom(ae::ObjProp prop) : NodeFor{prop} { living.insert(this); }
  ~MeshRoom() { living.erase(this); }
  inline static std::set<MeshRoom*> living;
  AE_OBJECT_REFLECT(AE_MMBR(text), AE_MMBR(participants))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) { dnv(base_, text, participants); }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const { dnv(base_, text, participants); }
  bool CanApply(MeshTextEvent const& event) const;
  bool CanApply(MeshJoinEvent const& event) const;
  void Apply(MeshTextEvent const& event);
  void Apply(MeshJoinEvent const& event);
  std::string text;
  std::vector<MeshParticipant::ptr> participants;
};

class MeshTextEvent : public EventFor<MeshRoom, MeshTextEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::MeshTextEvent", MeshTextEvent, Event, 0)
 protected:
  MeshTextEvent() = default;
 public:
  explicit MeshTextEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(text), AE_MMBR(author), AE_MMBR(room_reference))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) { dnv(base_, text, author, room_reference); }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const { dnv(base_, text, author, room_reference); }
  std::string text;
  MeshParticipant::ptr author;
  SharedNode::ptr room_reference;
};

class MeshJoinEvent : public EventFor<MeshRoom, MeshJoinEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::MeshJoinEvent", MeshJoinEvent, Event, 0)
 protected:
  MeshJoinEvent() = default;
 public:
  explicit MeshJoinEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(participant))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) { dnv(base_, participant); }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const { dnv(base_, participant); }
  MeshParticipant::ptr participant;
};

bool MeshRoom::CanApply(MeshTextEvent const& event) const {
  return event.author.is_valid() && std::any_of(participants.begin(), participants.end(),
      [&](auto const& participant) { return participant.id() == event.author.id(); });
}
bool MeshRoom::CanApply(MeshJoinEvent const& event) const {
  return event.participant.is_valid() && event.participant->link.is_valid() &&
      std::none_of(participants.begin(), participants.end(), [&](auto const& participant) {
        return participant->link->endpoint_uid == event.participant->link->endpoint_uid;
      });
}
void MeshRoom::Apply(MeshTextEvent const& event) { text += event.text; }
void MeshRoom::Apply(MeshJoinEvent const& event) {
  participants.push_back(event.participant);
  links.push_back(event.participant->link);
}

struct MeshExpected {
  ae::ObjId id;
  std::string source;
  AE_REFLECT_MEMBERS(id, source)
};
class MeshAttachEvent;
class MeshExpectEvent;
class MeshApplication : public NodeFor<MeshApplication> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::MeshApplication", MeshApplication, Node, 0)
 protected:
  MeshApplication() { living.insert(this); }
 public:
  explicit MeshApplication(ae::ObjProp prop) : NodeFor{prop} { living.insert(this); }
  ~MeshApplication() { living.erase(this); }
  inline static std::set<MeshApplication*> living;
  AE_OBJECT_REFLECT(AE_MMBR(rooms), AE_MMBR(expected))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) { dnv(base_, rooms, expected); }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const { dnv(base_, rooms, expected); }
  void Attach(SharedNode::ptr room);
  void Expect(ae::ObjId id, std::string source);
  void Apply(MeshAttachEvent const& event);
  void Apply(MeshExpectEvent const& event);
  std::vector<SharedNode::ptr> rooms;
  std::vector<MeshExpected> expected;
};
class MeshAttachEvent : public EventFor<MeshApplication, MeshAttachEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::MeshAttachEvent", MeshAttachEvent, Event, 0)
 protected:
  MeshAttachEvent() = default;
 public:
  explicit MeshAttachEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(room))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) { dnv(base_, room); }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const { dnv(base_, room); }
  SharedNode::ptr room;
};
class MeshExpectEvent : public EventFor<MeshApplication, MeshExpectEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::MeshExpectEvent", MeshExpectEvent, Event, 0)
 protected:
  MeshExpectEvent() = default;
 public:
  explicit MeshExpectEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(value))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) { dnv(base_, value); }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const { dnv(base_, value); }
  MeshExpected value;
};
void MeshApplication::Attach(SharedNode::ptr room) {
  auto event = MeshAttachEvent::ptr::Create(ae::CreateWith{*domain});
  event->room = std::move(room);
  Commit(std::move(event));
}
void MeshApplication::Expect(ae::ObjId id, std::string source) {
  auto event = MeshExpectEvent::ptr::Create(ae::CreateWith{*domain});
  event->value = {id, std::move(source)};
  Commit(std::move(event));
}
void MeshApplication::Apply(MeshAttachEvent const& event) {
  rooms.push_back(event.room);
  expected.erase(std::remove_if(expected.begin(), expected.end(),
      [&](auto const& item) { return item.id == event.room.id(); }), expected.end());
}
void MeshApplication::Apply(MeshExpectEvent const& event) { expected.push_back(event.value); }

APPTRAVERSE_REGISTER(MeshParticipant);
APPTRAVERSE_REGISTER(MeshRoom);
APPTRAVERSE_REGISTER(MeshTextEvent);
APPTRAVERSE_REGISTER(MeshJoinEvent);
APPTRAVERSE_REGISTER(MeshApplication);
APPTRAVERSE_REGISTER(MeshAttachEvent);
APPTRAVERSE_REGISTER(MeshExpectEvent);

MeshParticipant::ptr MakeParticipant(ae::Domain& domain, std::string uid, std::string name) {
  auto descriptor = SharedLinkDescriptor::ptr::Create(ae::CreateWith{domain});
  descriptor->endpoint_uid = std::move(uid);
  InitializeRuntimeNode(*descriptor);
  auto participant = MeshParticipant::ptr::Create(ae::CreateWith{domain});
  participant->name = std::move(name);
  participant->link = descriptor;
  InitializeRuntimeNode(*participant);
  return participant;
}

struct MeshDurable {
  ae::RamDomainStorage storage;
  ae::ObjId app_id;
  std::size_t saves{0};
};
struct MeshSent {
  std::string source, destination;
  SharedSyncMessage message;
  std::vector<std::uint8_t> bytes;
};
class MeshCountingLink final : public IByteLink {
 public:
  using Observer = std::function<void(std::string const&, std::string const&,
      SharedSyncMessage const&, std::vector<std::uint8_t> const&)>;
  MeshCountingLink(MemoryLink& link, Observer observer)
      : link_{link}, observer_{std::move(observer)} {}
  void SetReceiver(Receiver receiver) override { link_.SetReceiver(std::move(receiver)); }
  bool Send(std::string const& destination, std::vector<std::uint8_t> const& bytes) override {
    SharedSyncMessage message;
    bool const parsed = DeserializeSharedSyncMessage(bytes, message);
    CHECK(parsed);
    observer_(link_.EndpointId(), destination, message, bytes);
    return link_.Send(destination, bytes);
  }
  LinkAvailability Availability(std::string const& destination) const override {
    return link_.Availability(destination);
  }
 private:
  MemoryLink& link_;
  Observer observer_;
};

struct MeshReplica {
  std::string uid;
  MeshDurable& durable;
  ae::RamDomainStorage storage;
  std::unique_ptr<ae::Domain> domain;
  MeshApplication::ptr app;
  std::unique_ptr<MemoryLink> endpoint;
  std::unique_ptr<MeshCountingLink> transport;
  std::unique_ptr<SharedSyncRuntime> runtime;

  MeshReplica(MemoryLinkNetwork& network, std::string endpoint_uid, MeshDurable& disk,
              MeshCountingLink::Observer observer) : uid{std::move(endpoint_uid)}, durable{disk} {
    storage = durable.storage;
    domain = std::make_unique<ae::Domain>(storage);
    if (durable.app_id.is_valid()) {
      ae::DomainGraph graph{domain.get()};
      auto held = graph.LoadRoot(durable.app_id);
      CHECK(held && held->GetClassId() == MeshApplication::kClassId);
      app = MeshApplication::ptr{domain.get(), durable.app_id, {}, held};
    } else {
      app = MeshApplication::ptr::Create(ae::CreateWith{*domain});
      app->rooms.clear();
      app->expected.clear();
      InitializeRuntimeNode(*app);
      bool const saved = Persist();
      CHECK(saved);
    }
    endpoint = std::make_unique<MemoryLink>(network, uid);
    transport = std::make_unique<MeshCountingLink>(*endpoint, std::move(observer));
    runtime = std::make_unique<SharedSyncRuntime>(*domain, storage, *transport, uid,
        [this] { return Persist(); }, kRetry);
    for (auto const& room : app->rooms) { runtime->RegisterNode(room); }
    for (auto const& item : app->expected) { Bind(item); }
  }
  ~MeshReplica() {
    auto const saves = durable.saves;
    auto* const old_app = &*app;
    std::vector<MeshRoom*> old_rooms;
    for (auto const& room : app->rooms) {
      auto held = domain->Find(room.id());
      CHECK(held && held->GetClassId() == MeshRoom::kClassId);
      old_rooms.push_back(held.as<MeshRoom>());
      CHECK(old_rooms.back());
    }
    runtime.reset(); transport.reset(); endpoint.reset(); app.Reset(); domain.reset();
    CHECK(durable.saves == saves);
    CHECK(MeshApplication::living.count(old_app) == 0);
    for (auto* room : old_rooms) { CHECK(MeshRoom::living.count(room) == 0); }
  }
  bool Persist() {
    ae::DomainGraph graph{domain.get()};
    auto const saved = graph.SaveRoot(domain->Find(app.id()), app.id());
    if (saved.IsErr()) { return false; }
    durable.storage = storage;
    durable.app_id = app.id();
    ++durable.saves;
    return true;
  }
  void Bind(MeshExpected const& item) {
    runtime->ExpectInitialState(item.id, item.source, MeshRoom::kClassId,
        [this](SharedNode::ptr room) { app->Attach(std::move(room)); });
  }
  void Expect(ae::ObjId id) {
    app->Expect(id, "H");
    bool const saved = Persist();
    CHECK(saved);
    Bind(app->expected.back());
  }
  MeshRoom& Room(ae::ObjId id) const {
    auto root = runtime->FindNode(id);
    CHECK(root.is_valid());
    auto held = domain->Find(id);
    CHECK(held && held->GetClassId() == MeshRoom::kClassId);
    auto* room = held.as<MeshRoom>();
    CHECK(room);
    return *room;
  }
  SharedPeerState& Peer(ae::ObjId id, std::string const& endpoint_uid) const {
    for (auto const& peer : Room(id).local_state->peers) {
      if (peer->link->endpoint_uid == endpoint_uid) { return *peer; }
    }
    CHECK(false); std::abort();
  }
  ae::ObjId Append(ae::ObjId id, std::string text, std::uint64_t timestamp) {
    auto& room = Room(id);
    auto author = std::find_if(room.participants.begin(), room.participants.end(),
        [&](auto const& person) { return person->link->endpoint_uid == uid; });
    CHECK(author != room.participants.end());
    auto event = MeshTextEvent::ptr::Create(ae::CreateWith{*domain});
    event->text = std::move(text);
    event->author = *author;
    event->room_reference = runtime->FindNode(id);
    auto const event_id = event.id();
    bool const committed = runtime->CommitLocal(id, event, timestamp);
    CHECK(committed && !runtime->failed());
    return event_id;
  }
};

struct MeshObserved {
  ae::ObjId id;
  std::uint64_t timestamp;
  std::uint32_t class_id;
  std::string payload;
};
MeshObserved Describe(ae::Domain& domain, ae::ObjId id, std::uint64_t timestamp) {
  auto event = domain.Find(id);
  CHECK(event);
  if (event->GetClassId() == MeshTextEvent::kClassId) {
    auto* text = event.as<MeshTextEvent>();
    return {id, timestamp, MeshTextEvent::kClassId,
              text->author->link->endpoint_uid + ":" + text->text};
  }
  CHECK(event->GetClassId() == MeshJoinEvent::kClassId);
  auto* join = event.as<MeshJoinEvent>();
  return {id, timestamp, MeshJoinEvent::kClassId,
            join->participant->name + ":" + join->participant->link->endpoint_uid};
}
std::vector<MeshObserved> Observe(ae::Domain& domain, MeshRoom const& room) {
  std::vector<MeshObserved> result;
  std::string text = "room:";
  for (auto const& record : room.journal) {
    result.push_back(Describe(domain, record.event.id(), record.order.timestamp_us));
    auto event = domain.Find(record.event.id());
    CHECK(event);
    if (event->GetClassId() == MeshTextEvent::kClassId) {
      text += event.as<MeshTextEvent>()->text;
    }
  }
  CHECK(text == room.text);
  std::set<std::string> names;
  for (auto const& person : room.participants) {
    CHECK(names.insert(person->link->endpoint_uid).second);
    auto const descriptor = std::find_if(room.links.begin(), room.links.end(),
        [&](auto const& link) { return link->endpoint_uid == person->link->endpoint_uid; });
    CHECK(descriptor != room.links.end() && &**descriptor == &*person->link);
  }
  CHECK(room.links.size() == room.participants.size());
  return result;
}

struct Mesh {
  MemoryLinkNetwork network;
  std::map<std::string, MeshDurable> disks;
  std::map<std::string, std::unique_ptr<MeshReplica>> replicas;
  std::vector<MeshSent> sent;
  ae::ObjId room_id, h10, a20, join_id;
  std::size_t ack_checks{0};

  Mesh() {
    for (auto const& uid : {"H", "A", "B"}) { disks.emplace(uid, MeshDurable{}); Restart(uid); }
    auto& host = R("H");
    auto room = MeshRoom::ptr::Create(ae::CreateWith{*host.domain});
    room->text = "room:";
    room->participants.clear();
    for (auto const& uid : {"H", "A"}) {
      auto participant = MakeParticipant(*host.domain, uid, uid == std::string{"H"} ? "Host" : "Alice");
      room->participants.push_back(participant);
      room->links.push_back(participant->link);
    }
    SharedSyncRuntime::InitializeLocalState(*room);
    InitializeRuntimeNode(*room);
    room_id = room.id();
    host.app->Attach(room);
    host.runtime->RegisterNode(room);
    R("A").Expect(room_id);
    bool const initial = host.runtime->QueueInitialState(room_id, "A");
    CHECK(initial);
    Service(); Drain();
    h10 = R("H").Append(room_id, "|H10", 10);
    Service(); Drain();
    a20 = R("A").Append(room_id, "|A20", 20);
    Service(); Drain();
    CHECK(R("A").Peer(room_id, "H").HasDelivered(a20));
    R("B").Expect(room_id);
  }
  MeshReplica& R(std::string const& uid) const { return *replicas.at(uid); }
  void Restart(std::string const& uid) {
    replicas[uid].reset();
    auto observer = [this](auto const& source, auto const& destination,
                           auto const& message, auto const& bytes) {
      if (message.kind == SharedSyncKind::Ack) { CheckAck(source, destination, message); }
      sent.push_back({source, destination, message, bytes});
    };
    replicas[uid] = std::make_unique<MeshReplica>(network, uid, disks.at(uid), observer);
  }
  void Admit() {
    auto& host = R("H");
    auto participant = MakeParticipant(*host.domain, "B", "Bob");
    auto event = MeshJoinEvent::ptr::Create(ae::CreateWith{*host.domain});
    event->participant = participant;
    join_id = event.id();
    bool const admitted = host.runtime->AdmitPeer(room_id, event, "B", 30);
    CHECK(admitted);
  }
  void Service(std::uint64_t advance = 0) {
    network.AdvanceTo(network.NowUs() + advance);
    for (auto const& [_, replica] : replicas) {
      replica->runtime->Service(network.NowUs());
      CHECK(!replica->runtime->failed());
    }
  }
  SharedSyncMessage Decode(MemoryLinkNetwork::Packet const& packet) const {
    SharedSyncMessage message;
    bool const decoded = DeserializeSharedSyncMessage(packet.bytes, message);
    CHECK(decoded);
    return message;
  }
  std::optional<MemoryLinkNetwork::Packet> Find(SharedSyncKind kind,
      std::string const& source, std::string const& destination,
      ae::ObjId event_id = {}, std::optional<SharedSyncKind> ack_kind = {}) const {
    for (auto const& packet : network.PendingPackets()) {
      auto const message = Decode(packet);
      if (message.kind == kind && packet.source == source && packet.destination == destination &&
          (!event_id.is_valid() || message.event_id == event_id) &&
          (!ack_kind.has_value() || message.acknowledged_kind == *ack_kind)) { return packet; }
    }
    return {};
  }
  MemoryLinkNetwork::Packet Packet(SharedSyncKind kind, std::string const& source,
      std::string const& destination, ae::ObjId event_id = {},
      std::optional<SharedSyncKind> ack_kind = {}) const {
    auto packet = Find(kind, source, destination, event_id, ack_kind);
    CHECK(packet.has_value());
    return *packet;
  }
  void Deliver(MemoryLinkNetwork::Packet const& packet) {
    bool const delivered = network.Deliver(packet.id); CHECK(delivered);
  }
  void Drop(MemoryLinkNetwork::Packet const& packet) {
    bool const dropped = network.Drop(packet.id); CHECK(dropped);
  }
  void Drain(bool reverse = false) {
    for (unsigned turn = 0; turn != 2000; ++turn) {
      auto packets = network.PendingPackets();
      if (reverse) { std::reverse(packets.begin(), packets.end()); }
      bool progress = false;
      for (auto const& packet : packets) {
        if (network.Deliver(packet.id)) { progress = true; break; }
      }
      if (!progress) { return; }
    }
    CHECK(false);
  }
  bool Pending() const {
    if (!network.PendingPackets().empty()) { return true; }
    for (auto const& [_, replica] : replicas) {
      for (auto const& room : replica->app->rooms) {
        for (auto const& peer : room->local_state->peers) {
          if (!peer->pending_packets.empty()) { return true; }
        }
      }
    }
    return false;
  }
  void Settle(bool reverse = false) {
    for (unsigned turn = 0; turn != 12; ++turn) {
      Service(turn == 0 ? 0 : kRetry);
      Drain(reverse);
      if (!Pending()) { return; }
    }
    CHECK(false);
  }
  std::size_t Count(std::string const& source, std::string const& destination,
                    ae::ObjId event_id) const {
    return std::count_if(sent.begin(), sent.end(), [&](auto const& frame) {
      return frame.source == source && frame.destination == destination &&
             frame.message.kind == SharedSyncKind::Event && frame.message.event_id == event_id;
    });
  }
  void NoOldResend() const { CHECK(Count("A", "B", a20) == 0); }
  void Equal(std::size_t count) const {
    auto const reference = Observe(*R("H").domain, R("H").Room(room_id));
    CHECK(reference.size() == count);
    for (auto const& uid : {"A", "B"}) {
      auto const actual = Observe(*R(uid).domain, R(uid).Room(room_id));
      CHECK(actual.size() == count);
      CHECK(R(uid).Room(room_id).text == R("H").Room(room_id).text);
      for (std::size_t i = 0; i != count; ++i) {
        CHECK(actual[i].id == reference[i].id && actual[i].timestamp == reference[i].timestamp &&
              actual[i].class_id == reference[i].class_id && actual[i].payload == reference[i].payload);
      }
      CHECK(R(uid).Room(room_id).participants.size() == 3);
      CHECK(R(uid).app->rooms.size() == 1);
    }
    CHECK(R("H").Room(room_id).participants.size() == 3);
    CHECK(R("A").Peer(room_id, "B").coverage_confirmed);
    auto const snapshot_id = R("A").Peer(room_id, "B").admission.snapshot_id;
    CHECK(snapshot_id.is_valid());
    CHECK(R("H").Peer(room_id, "A").HasNoticeDelivered(snapshot_id));
    NoOldResend();
  }
  void CheckAck(std::string const& source, std::string const& destination,
                SharedSyncMessage const& ack) {
    auto original = std::find_if(sent.begin(), sent.end(), [&](auto const& frame) {
      return frame.source == destination && frame.destination == source &&
          frame.message.node_id == ack.node_id && frame.message.packet_id == ack.packet_id &&
          frame.message.kind == ack.acknowledged_kind;
    });
    CHECK(original != sent.end());
    auto storage = disks.at(source).storage;
    ae::Domain domain{storage};
    ae::DomainGraph graph{&domain};
    auto held = graph.LoadRoot(disks.at(source).app_id);
    CHECK(held && held->GetClassId() == MeshApplication::kClassId);
    auto* app = held.as<MeshApplication>();
    MeshRoom* room = nullptr;
    for (auto const& item : app->rooms) {
      if (item.id() == ack.node_id) {
        auto item_held = domain.Find(item.id());
        CHECK(item_held && item_held->GetClassId() == MeshRoom::kClassId);
        room = item_held.as<MeshRoom>();
      }
    }
    CHECK(room);
    if (ack.acknowledged_kind == SharedSyncKind::CoverageReceipt) {
      auto const& plan = original->message.admission;
      auto peer = std::find_if(room->local_state->peers.begin(), room->local_state->peers.end(),
          [&](auto const& value) { return value->link->endpoint_uid == plan.joining_endpoint; });
      CHECK(peer != room->local_state->peers.end());
      CHECK((*peer)->coverage_confirmed && (*peer)->admission == plan);
      for (auto id : plan.covered_events) { CHECK((*peer)->HasDelivered(id)); }
    } else {
      auto const actual = Observe(domain, *room);
      ae::RamDomainStorage expected_storage;
      expected_storage.state = original->message.graph.layers;
      ae::Domain expected_domain{expected_storage};
      ae::DomainGraph expected_graph{&expected_domain, ae::GraphSerializationScope::NetworkShared};
      auto expected_root = expected_graph.LoadRoot(original->message.graph.root_id);
      CHECK(expected_root);
      std::vector<MeshObserved> expected;
      if (ack.acknowledged_kind == SharedSyncKind::InitialState) {
        CHECK(expected_root->GetClassId() == MeshRoom::kClassId);
        auto* initial_room = expected_root.as<MeshRoom>();
        CHECK(initial_room);
        expected = Observe(expected_domain, *initial_room);
      } else {
        expected.push_back(Describe(expected_domain, original->message.event_id,
                                     original->message.timestamp_us));
      }
      for (auto const& item : expected) {
        auto found = std::find_if(actual.begin(), actual.end(),
            [&](auto const& value) { return value.id == item.id; });
        CHECK(found != actual.end());
        CHECK(found->timestamp == item.timestamp && found->class_id == item.class_id &&
              found->payload == item.payload);
      }
    }
    ++ack_checks;
  }
};

void TestJoinFirstImmediateDirectDelivery() {
  Mesh mesh;
  mesh.Admit(); mesh.Service();
  auto const snapshot = mesh.Packet(SharedSyncKind::InitialState, "H", "B");
  mesh.Deliver(mesh.Packet(SharedSyncKind::Event, "H", "A", mesh.join_id));
  CHECK(mesh.R("A").Room(mesh.room_id).participants.size() == 3);
  auto const& assigned = mesh.R("A").Peer(mesh.room_id, "B");
  CHECK(assigned.IsCoveredByAdmission(mesh.a20));
  CHECK(!assigned.coverage_confirmed);
  CHECK(!assigned.HasDelivered(mesh.a20));  // Assignment is not an ACK.
  CHECK(!mesh.Find(SharedSyncKind::CoverageReceipt, "H", "A").has_value());
  CHECK(!mesh.R("H").Peer(mesh.room_id, "A").FindPending(
      mesh.Decode(snapshot).packet_id));
  mesh.NoOldResend();
  auto const a40 = mesh.R("A").Append(mesh.room_id, "|A40", 40);
  mesh.Service();
  CHECK(mesh.Count("A", "H", a40) == 1 && mesh.Count("A", "B", a40) == 1);
  mesh.Deliver(mesh.Packet(SharedSyncKind::Event, "A", "B", a40));
  CHECK(mesh.R("B").app->rooms.empty());
  CHECK(!mesh.Find(SharedSyncKind::Ack, "B", "A", a40).has_value());
  mesh.Deliver(mesh.Packet(SharedSyncKind::Event, "A", "H", a40));
  mesh.Deliver(snapshot);
  mesh.Settle(true);
  mesh.Equal(4);
  auto const b50 = mesh.R("B").Append(mesh.room_id, "|B50", 50);
  auto const h60 = mesh.R("H").Append(mesh.room_id, "|H60", 60);
  mesh.Settle(true); mesh.Equal(6);
  CHECK(mesh.Count("B", "H", b50) >= 1 && mesh.Count("B", "A", b50) >= 1);
  CHECK(mesh.Count("H", "A", h60) >= 1 && mesh.Count("H", "B", h60) >= 1);
  CHECK(mesh.Count("H", "A", b50) == 0 && mesh.Count("H", "B", a40) == 0);
}

void TestSnapshotAndReceiptBeforeJoin() {
  Mesh mesh;
  mesh.Admit(); mesh.Service();
  auto const join = mesh.Packet(SharedSyncKind::Event, "H", "A", mesh.join_id);
  mesh.Deliver(mesh.Packet(SharedSyncKind::InitialState, "H", "B"));
  mesh.Deliver(mesh.Packet(SharedSyncKind::Ack, "B", "H", {}, SharedSyncKind::InitialState));
  mesh.Service();
  auto const notice = mesh.Packet(SharedSyncKind::CoverageReceipt, "H", "A");
  auto const b50 = mesh.R("B").Append(mesh.room_id, "|B50", 50);
  mesh.Service();
  mesh.Deliver(mesh.Packet(SharedSyncKind::Event, "B", "A", b50));
  CHECK(mesh.R("A").Room(mesh.room_id).participants.size() == 2);
  CHECK(!mesh.Find(SharedSyncKind::Ack, "A", "B", b50).has_value());
  mesh.Deliver(notice);
  CHECK(!mesh.Find(SharedSyncKind::Ack, "A", "H", {}, SharedSyncKind::CoverageReceipt).has_value());
  CHECK(mesh.R("H").Peer(mesh.room_id, "A").FindPending(mesh.join_id));
  mesh.Deliver(join);
  mesh.Settle(true); mesh.Equal(4);
}

void TestMissingOwnHistoryUsesOriginalFrameAfterHostAck() {
  Mesh mesh;
  auto const a25 = mesh.R("A").Append(mesh.room_id, "|A25", 25);
  mesh.Service();
  auto const original = mesh.Packet(SharedSyncKind::Event, "A", "H", a25);
  mesh.Admit(); mesh.Service();  // The frozen snapshot does not contain A25.
  mesh.Deliver(original);
  mesh.Deliver(mesh.Packet(SharedSyncKind::Ack, "H", "A", a25));
  CHECK(mesh.R("A").Peer(mesh.room_id, "H").FindPending(a25) == nullptr);
  mesh.Deliver(mesh.Packet(SharedSyncKind::Event, "H", "A", mesh.join_id));
  mesh.Service();
  auto const direct = mesh.Packet(SharedSyncKind::Event, "A", "B", a25);
  CHECK(direct.bytes == original.bytes);
  mesh.Drop(direct);
  mesh.NoOldResend();
  mesh.Restart("A");
  mesh.Settle(true); mesh.Equal(4);
  CHECK(mesh.Count("A", "B", a25) >= 2);
}

void TestSnapshotReceiptDoesNotAcknowledgeAnotherRecipient() {
  Mesh mesh;
  auto const h25 = mesh.R("H").Append(mesh.room_id, "|H25", 25);
  mesh.Service();
  auto const old_to_a = mesh.Packet(SharedSyncKind::Event, "H", "A", h25);
  mesh.Admit(); mesh.Service();
  mesh.Deliver(mesh.Packet(SharedSyncKind::InitialState, "H", "B"));
  mesh.Deliver(mesh.Packet(SharedSyncKind::Ack, "B", "H", {}, SharedSyncKind::InitialState));
  auto const* pending = mesh.R("H").Peer(mesh.room_id, "A").FindPending(h25);
  CHECK(pending && pending->bytes == old_to_a.bytes);
  mesh.Drop(old_to_a);
  mesh.Deliver(mesh.Packet(SharedSyncKind::Event, "H", "A", mesh.join_id));
  mesh.Settle(true); mesh.Equal(4);
  CHECK(mesh.Count("H", "A", h25) >= 2);
  CHECK(mesh.Count("H", "B", h25) == 0);
}

void TestLostControlPacketsAndRestartEveryPhase() {
  Mesh mesh;
  mesh.Admit(); mesh.Service();
  auto const first_join = mesh.Packet(SharedSyncKind::Event, "H", "A", mesh.join_id);
  auto const first_snapshot = mesh.Packet(SharedSyncKind::InitialState, "H", "B");
  mesh.Drop(first_join); mesh.Drop(first_snapshot);
  mesh.Restart("H"); mesh.Restart("A"); mesh.Restart("B");
  mesh.Service(kRetry);
  auto const join = mesh.Packet(SharedSyncKind::Event, "H", "A", mesh.join_id);
  auto const snapshot = mesh.Packet(SharedSyncKind::InitialState, "H", "B");
  CHECK(join.bytes == first_join.bytes && snapshot.bytes == first_snapshot.bytes);
  mesh.Deliver(join);
  mesh.Drop(mesh.Packet(SharedSyncKind::Ack, "A", "H", mesh.join_id));
  mesh.Deliver(snapshot);
  mesh.Drop(mesh.Packet(SharedSyncKind::Ack, "B", "H", {}, SharedSyncKind::InitialState));
  CHECK(!mesh.R("H").Peer(mesh.room_id, "B").initial_complete);
  mesh.Restart("A"); mesh.Restart("B"); mesh.Restart("H");
  mesh.Service(kRetry);
  auto const retry_snapshot = mesh.Packet(SharedSyncKind::InitialState, "H", "B");
  CHECK(retry_snapshot.bytes == first_snapshot.bytes);
  mesh.Deliver(retry_snapshot);
  mesh.Deliver(mesh.Packet(SharedSyncKind::Ack, "B", "H", {}, SharedSyncKind::InitialState));
  mesh.Restart("H");  // Receipt obligation must already be durable.
  mesh.Service();
  auto const notice = mesh.Packet(SharedSyncKind::CoverageReceipt, "H", "A");
  mesh.Drop(notice);
  mesh.Restart("H"); mesh.Restart("A");
  mesh.Service(kRetry);
  auto const retry_notice = mesh.Packet(SharedSyncKind::CoverageReceipt, "H", "A");
  CHECK(retry_notice.bytes == notice.bytes);
  mesh.Deliver(retry_notice);
  auto const receipt_ack = mesh.Packet(SharedSyncKind::Ack, "A", "H", {}, SharedSyncKind::CoverageReceipt);
  mesh.Drop(receipt_ack);
  CHECK(mesh.R("A").Peer(mesh.room_id, "B").coverage_confirmed);
  mesh.Restart("A"); mesh.Restart("H");
  mesh.Service(kRetry);
  CHECK(mesh.Packet(SharedSyncKind::CoverageReceipt, "H", "A").bytes == notice.bytes);
  mesh.Settle(true); mesh.Equal(3);
  auto const a40 = mesh.R("A").Append(mesh.room_id, "|A40", 40);
  mesh.Settle(); mesh.Equal(4);
  CHECK(mesh.Count("A", "B", a40) == 1);
  CHECK(mesh.ack_checks >= 10);
}

void TestOfflineAndAllThreeRestartWithDirectPending() {
  Mesh mesh;
  mesh.Admit(); mesh.Settle(true); mesh.Equal(3);
  mesh.network.SetAvailability("B", LinkAvailability::Offline);
  auto const a40 = mesh.R("A").Append(mesh.room_id, "|A40", 40);
  auto const h50 = mesh.R("H").Append(mesh.room_id, "|H50", 50);
  mesh.Service(); mesh.Drain(true);
  CHECK(mesh.R("H").Room(mesh.room_id).text == mesh.R("A").Room(mesh.room_id).text);
  CHECK(mesh.R("B").Room(mesh.room_id).journal.size() == 3);
  CHECK(mesh.Count("A", "B", a40) == 0 && mesh.Count("H", "B", h50) == 0);
  mesh.Service(100 * kRetry); mesh.Drain();
  CHECK(mesh.Count("A", "B", a40) == 0 && mesh.Count("H", "B", h50) == 0);
  mesh.Restart("H"); mesh.Restart("A"); mesh.Restart("B");
  mesh.network.SetAvailability("B", LinkAvailability::Offline);
  mesh.Service(kRetry); mesh.Drain();
  CHECK(mesh.Count("A", "B", a40) == 0 && mesh.Count("H", "B", h50) == 0);
  mesh.network.SetAvailability("B", LinkAvailability::Online);
  mesh.Service();
  auto const packet = mesh.Packet(SharedSyncKind::Event, "A", "B", a40);
  auto const duplicate = mesh.network.Duplicate(packet.id); CHECK(duplicate != 0);
  mesh.Settle(true); mesh.Equal(5);
  auto const b60 = mesh.R("B").Append(mesh.room_id, "|B60", 60);
  mesh.Settle(true); mesh.Equal(6);
  CHECK(mesh.Count("B", "A", b60) == 1 && mesh.Count("B", "H", b60) == 1);
  auto const a70 = mesh.R("A").Append(mesh.room_id, "|A70", 70);
  auto const b80 = mesh.R("B").Append(mesh.room_id, "|B80", 80);
  auto const h90 = mesh.R("H").Append(mesh.room_id, "|H90", 90);
  CHECK(mesh.network.PendingPackets().empty());
  mesh.Restart("H"); mesh.Restart("A"); mesh.Restart("B");
  mesh.Settle(true); mesh.Equal(9);
  CHECK(mesh.Count("A", "H", a70) == 1 && mesh.Count("A", "B", a70) == 1);
  CHECK(mesh.Count("B", "H", b80) == 1 && mesh.Count("B", "A", b80) == 1);
  CHECK(mesh.Count("H", "A", h90) == 1 && mesh.Count("H", "B", h90) == 1);
  CHECK(mesh.Count("H", "B", a70) == 0 && mesh.Count("A", "H", b80) == 0);
}

void TestHostOfflineLeavesDirectClientsWorking() {
  Mesh mesh;
  mesh.Admit(); mesh.Settle(true); mesh.Equal(3);
  mesh.network.SetAvailability("H", LinkAvailability::Offline);
  auto const a40 = mesh.R("A").Append(mesh.room_id, "|A40", 40);
  auto const b50 = mesh.R("B").Append(mesh.room_id, "|B50", 50);
  mesh.Service(); mesh.Drain(true);
  CHECK(mesh.R("A").Room(mesh.room_id).text == mesh.R("B").Room(mesh.room_id).text);
  CHECK(mesh.R("A").Room(mesh.room_id).journal.size() == 5);
  CHECK(mesh.R("H").Room(mesh.room_id).journal.size() == 3);
  CHECK(mesh.Count("A", "B", a40) == 1 && mesh.Count("B", "A", b50) == 1);
  CHECK(mesh.Count("A", "H", a40) == 0 && mesh.Count("B", "H", b50) == 0);
  mesh.Service(10 * kRetry); mesh.Drain();
  CHECK(mesh.Count("A", "H", a40) == 0 && mesh.Count("B", "H", b50) == 0);
  mesh.Restart("H");
  mesh.network.SetAvailability("H", LinkAvailability::Offline);
  mesh.Service(kRetry); mesh.Drain();
  CHECK(mesh.Count("A", "H", a40) == 0 && mesh.Count("B", "H", b50) == 0);
  mesh.network.SetAvailability("H", LinkAvailability::Online);
  mesh.Settle(true); mesh.Equal(5);
  CHECK(mesh.Count("A", "H", a40) == 1 && mesh.Count("B", "H", b50) == 1);
  CHECK(mesh.Count("H", "A", a40) == 0 && mesh.Count("H", "B", a40) == 0);
  CHECK(mesh.Count("H", "A", b50) == 0 && mesh.Count("H", "B", b50) == 0);
  auto const h60 = mesh.R("H").Append(mesh.room_id, "|H60", 60);
  mesh.Settle(); mesh.Equal(6);
  CHECK(mesh.Count("H", "A", h60) == 1 && mesh.Count("H", "B", h60) == 1);
}

void TestLegacyNativeEventAfterAdmission() {
  Mesh mesh;
  mesh.Admit(); mesh.Settle(true); mesh.Equal(3);
  auto const a40 = mesh.R("A").Append(mesh.room_id, "|A40", 40);
  mesh.Service();
  auto const current = mesh.Packet(SharedSyncKind::Event, "A", "B", a40);
  auto legacy = mesh.Decode(current);
  CHECK(!legacy.admission.is_valid());
  legacy.minor = 0;
  std::vector<std::uint8_t> bytes;
  auto unsupported = legacy;
  unsupported.admission = mesh.R("A").Peer(mesh.room_id, "B").admission;
  bool const admission_downgraded = SerializeSharedSyncMessage(unsupported, bytes);
  CHECK(!admission_downgraded);
  unsupported.admission = {};
  unsupported.kind = SharedSyncKind::CoverageReceipt;
  bool const receipt_downgraded = SerializeSharedSyncMessage(unsupported, bytes);
  CHECK(!receipt_downgraded);
  unsupported.kind = SharedSyncKind::Ack;
  unsupported.acknowledged_kind = SharedSyncKind::CoverageReceipt;
  bool const receipt_ack_downgraded = SerializeSharedSyncMessage(unsupported, bytes);
  CHECK(!receipt_ack_downgraded);
  bool const encoded = SerializeSharedSyncMessage(legacy, bytes);
  CHECK(encoded);
  SharedSyncMessage decoded;
  bool const parsed = DeserializeSharedSyncMessage(bytes, decoded);
  CHECK(parsed && decoded.major == 1 && decoded.minor == 0);
  CHECK(decoded.event_id == a40 && decoded.graph.layers == legacy.graph.layers);
  mesh.Drop(current);
  // Preserve the real outgoing-frame ledger entry but deliberately replace
  // its wire representation with the previous native tuple layout.
  bool const sent = mesh.R("A").endpoint->Send("B", bytes);
  CHECK(sent);
  auto const packet = mesh.Packet(SharedSyncKind::Event, "A", "B", a40);
  CHECK(mesh.Decode(packet).minor == 0);
  mesh.Deliver(packet);
  auto const ack = mesh.Packet(SharedSyncKind::Ack, "B", "A", a40);
  CHECK(mesh.Decode(ack).minor == 0);
  mesh.Deliver(ack);
  CHECK(mesh.R("A").Peer(mesh.room_id, "B").HasDelivered(a40));
  CHECK(mesh.R("A").Peer(mesh.room_id, "B").FindPending(a40) == nullptr);
  mesh.Settle(true); mesh.Equal(4);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureSharedNodeRegistration();
  apptraverse::test::TestJoinFirstImmediateDirectDelivery();
  apptraverse::test::TestSnapshotAndReceiptBeforeJoin();
  apptraverse::test::TestMissingOwnHistoryUsesOriginalFrameAfterHostAck();
  apptraverse::test::TestSnapshotReceiptDoesNotAcknowledgeAnotherRecipient();
  apptraverse::test::TestLostControlPacketsAndRestartEveryPhase();
  apptraverse::test::TestOfflineAndAllThreeRestartWithDirectPending();
  apptraverse::test::TestHostOfflineLeavesDirectClientsWorking();
  apptraverse::test::TestLegacyNativeEventAfterAdmission();
  std::cout << "shared_three_peer_test OK\n";
}
