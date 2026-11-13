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
    if (!(condition)) {                                                     \
      std::cerr << "CHECK failed: " #condition << " at " << __FILE__ << ':' \
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
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, name, link);
  }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, name, link);
  }
  std::string name;
  SharedLinkDescriptor::ptr link;
};

class MeshTextEvent;
class MeshJoinEvent;
class MeshRoom : public NodeFor<MeshRoom, SharedNode> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::MeshRoom", MeshRoom, SharedNode,
                           0)
 protected:
  MeshRoom() { living.insert(this); }

 public:
  explicit MeshRoom(ae::ObjProp prop) : NodeFor{prop} { living.insert(this); }
  ~MeshRoom() { living.erase(this); }
  inline static std::set<MeshRoom*> living;
  AE_OBJECT_REFLECT(AE_MMBR(text), AE_MMBR(participants))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, text, participants);
  }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, text, participants);
  }
  bool CanApply(MeshTextEvent const& event) const;
  bool CanApply(MeshJoinEvent const& event) const;
  void Apply(MeshTextEvent const& event);
  void Apply(MeshJoinEvent const& event);
  std::string text;
  std::vector<MeshParticipant::ptr> participants;
};

class MeshTextEvent : public EventFor<MeshRoom, MeshTextEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::MeshTextEvent", MeshTextEvent,
                           Event, 0)
 protected:
  MeshTextEvent() = default;

 public:
  explicit MeshTextEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(text), AE_MMBR(author), AE_MMBR(room_reference))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, text, author, room_reference);
  }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, text, author, room_reference);
  }
  std::string text;
  MeshParticipant::ptr author;
  SharedNode::ptr room_reference;
};

class MeshJoinEvent : public EventFor<MeshRoom, MeshJoinEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::MeshJoinEvent", MeshJoinEvent,
                           Event, 0)
 protected:
  MeshJoinEvent() = default;

 public:
  explicit MeshJoinEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(participant))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, participant);
  }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, participant);
  }
  MeshParticipant::ptr participant;
};

bool MeshRoom::CanApply(MeshTextEvent const& event) const {
  return event.author.is_valid() &&
         std::any_of(participants.begin(), participants.end(),
                     [&](auto const& participant) {
                       return participant.id() == event.author.id();
                     });
}
bool MeshRoom::CanApply(MeshJoinEvent const& event) const {
  return event.participant.is_valid() && event.participant->link.is_valid() &&
         std::none_of(participants.begin(), participants.end(),
                      [&](auto const& participant) {
                        return participant->link->endpoint_uid ==
                               event.participant->link->endpoint_uid;
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
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::MeshApplication",
                           MeshApplication, Node, 0)
 protected:
  MeshApplication() { living.insert(this); }

 public:
  explicit MeshApplication(ae::ObjProp prop) : NodeFor{prop} {
    living.insert(this);
  }
  ~MeshApplication() { living.erase(this); }
  inline static std::set<MeshApplication*> living;
  AE_OBJECT_REFLECT(AE_MMBR(rooms), AE_MMBR(expected))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, rooms, expected);
  }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, rooms, expected);
  }
  void Attach(SharedNode::ptr room);
  void Expect(ae::ObjId id, std::string source);
  void Apply(MeshAttachEvent const& event);
  void Apply(MeshExpectEvent const& event);
  std::vector<SharedNode::ptr> rooms;
  std::vector<MeshExpected> expected;
};
class MeshAttachEvent : public EventFor<MeshApplication, MeshAttachEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::MeshAttachEvent",
                           MeshAttachEvent, Event, 0)
 protected:
  MeshAttachEvent() = default;

 public:
  explicit MeshAttachEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(room))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, room);
  }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, room);
  }
  SharedNode::ptr room;
};
class MeshExpectEvent : public EventFor<MeshApplication, MeshExpectEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::MeshExpectEvent",
                           MeshExpectEvent, Event, 0)
 protected:
  MeshExpectEvent() = default;

 public:
  explicit MeshExpectEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(value))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, value);
  }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, value);
  }
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
                                [&](auto const& item) {
                                  return item.id == event.room.id();
                                }),
                 expected.end());
}
void MeshApplication::Apply(MeshExpectEvent const& event) {
  expected.push_back(event.value);
}

APPTRAVERSE_REGISTER(MeshParticipant);
APPTRAVERSE_REGISTER(MeshRoom);
APPTRAVERSE_REGISTER(MeshTextEvent);
APPTRAVERSE_REGISTER(MeshJoinEvent);
APPTRAVERSE_REGISTER(MeshApplication);
APPTRAVERSE_REGISTER(MeshAttachEvent);
APPTRAVERSE_REGISTER(MeshExpectEvent);

MeshParticipant::ptr MakeParticipant(ae::Domain& domain, std::string uid,
                                     std::string name) {
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
                                      SharedSyncMessage const&,
                                      std::vector<std::uint8_t> const&)>;
  MeshCountingLink(MemoryLink& link, Observer observer)
      : link_{link}, observer_{std::move(observer)} {}
  void SetReceiver(Receiver receiver) override {
    link_.SetReceiver(std::move(receiver));
  }
  bool Send(std::string const& destination,
            std::vector<std::uint8_t> const& bytes) override {
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

  MeshReplica(MemoryLinkNetwork& network, std::string endpoint_uid,
              MeshDurable& disk, MeshCountingLink::Observer observer)
      : uid{std::move(endpoint_uid)}, durable{disk} {
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
    transport =
        std::make_unique<MeshCountingLink>(*endpoint, std::move(observer));
    runtime = std::make_unique<SharedSyncRuntime>(
        *domain, storage, *transport, uid, [this] { return Persist(); },
        kRetry);
    for (auto const& room : app->rooms) {
      runtime->RegisterNode(room);
    }
    for (auto const& item : app->expected) {
      Bind(item);
    }
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
    runtime.reset();
    transport.reset();
    endpoint.reset();
    app.Reset();
    domain.reset();
    CHECK(durable.saves == saves);
    CHECK(MeshApplication::living.count(old_app) == 0);
    for (auto* room : old_rooms) {
      CHECK(MeshRoom::living.count(room) == 0);
    }
  }
  bool Persist() {
    ae::RamDomainStorage checkpoint;
    SavePersistedObjectGraphToRam(*app, checkpoint);
    storage = std::move(checkpoint);
    durable.storage = storage;
    durable.app_id = app.id();
    ++durable.saves;
    return true;
  }
  void Bind(MeshExpected const& item) {
    runtime->ExpectInitialState(
        item.id, item.source, MeshRoom::kClassId,
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
      if (peer->link->endpoint_uid == endpoint_uid) {
        return *peer;
      }
    }
    CHECK(false);
    std::abort();
  }
  MeshTextEvent::ptr NewText(ae::ObjId id, std::string text) {
    auto& room = Room(id);
    auto author = std::find_if(
        room.participants.begin(), room.participants.end(),
        [&](auto const& person) { return person->link->endpoint_uid == uid; });
    CHECK(author != room.participants.end());
    auto event = MeshTextEvent::ptr::Create(ae::CreateWith{*domain});
    event->text = std::move(text);
    event->author = *author;
    event->room_reference = runtime->FindNode(id);
    return event;
  }
  ae::ObjId Append(ae::ObjId id, std::string text, std::uint64_t timestamp) {
    auto event = NewText(id, std::move(text));
    auto const event_id = event.id();
    bool const committed = runtime->CommitLocal(id, event, timestamp);
    CHECK(committed && !runtime->failed());
    return event_id;
  }
  // A pre-index wire origin: freeze old message bytes and keep unstamped local
  // origin/pending records, rather than downgrading an indexed 1.2 send.
  ae::ObjId AppendLegacy(ae::ObjId id, std::string text,
                         std::uint64_t timestamp, std::uint16_t minor = 0) {
    auto event = NewText(id, std::move(text));
    SharedSyncMessage message;
    message.minor = minor;
    message.node_id = id;
    message.packet_id = event.id();
    message.event_id = event.id();
    message.timestamp_us = timestamp;
    bool const captured = CaptureSharedGraph(*event, message.graph);
    CHECK(captured);
    SharedPendingPacket frame;
    frame.packet_id = event.id();
    frame.event_id = event.id();
    bool const encoded = SerializeSharedSyncMessage(message, frame.bytes);
    CHECK(encoded);
    auto& room = Room(id);
    bool const inserted = room.TryInsertJournalEvent(
        EventRecord{.event = event,
                    .order = {.timestamp_us = timestamp},
                    .retained_since_us = timestamp});
    CHECK(inserted);
    room.local_state->RememberLocalFrame(frame);
    for (auto const& peer : room.local_state->peers) {
      peer->QueuePacket(frame);
    }
    bool const saved = Persist();
    CHECK(saved);
    return event.id();
  }
};

struct MeshObserved {
  ae::ObjId id;
  std::uint64_t timestamp;
  std::uint32_t class_id;
  std::string payload;
};
MeshObserved Describe(ae::Domain& domain, ae::ObjId id,
                      std::uint64_t timestamp) {
  auto event = domain.Find(id);
  CHECK(event);
  if (event->GetClassId() == MeshTextEvent::kClassId) {
    auto* text = event.as<MeshTextEvent>();
    return {id, timestamp, MeshTextEvent::kClassId,
            text->author->link->endpoint_uid + ":" + text->text};
  }
  CHECK(event->GetClassId() == MeshJoinEvent::kClassId);
  auto* join = event.as<MeshJoinEvent>();
  return {
      id, timestamp, MeshJoinEvent::kClassId,
      join->participant->name + ":" + join->participant->link->endpoint_uid};
}
std::vector<MeshObserved> Observe(ae::Domain& domain, MeshRoom const& room,
                                  std::uint64_t checkpoint_us = 0) {
  std::vector<MeshObserved> result;
  std::string text = "room:";
  for (auto const& record : room.journal) {
    result.push_back(
        Describe(domain, record.event.id(), record.order.timestamp_us));
    auto event = domain.Find(record.event.id());
    CHECK(event);
    if (event->GetClassId() == MeshTextEvent::kClassId) {
      text += event.as<MeshTextEvent>()->text;
    }
  }
  if (checkpoint_us == 0 &&
      (!room.local_state.is_valid() || room.local_state->checkpoint_us == 0)) {
    CHECK(text == room.text);
  }
  std::set<std::string> names;
  for (auto const& person : room.participants) {
    CHECK(names.insert(person->link->endpoint_uid).second);
    auto const descriptor = std::find_if(
        room.links.begin(), room.links.end(), [&](auto const& link) {
          return link->endpoint_uid == person->link->endpoint_uid;
        });
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

  explicit Mesh(bool bootstrap = true) {
    for (auto const& uid : {"H", "A", "B"}) {
      disks.emplace(uid, MeshDurable{});
      Restart(uid);
    }
    if (!bootstrap) return;
    auto& host = R("H");
    auto room = MeshRoom::ptr::Create(ae::CreateWith{*host.domain});
    room->text = "room:";
    room->participants.clear();
    for (auto const& uid : {"H", "A"}) {
      auto participant = MakeParticipant(
          *host.domain, uid, uid == std::string{"H"} ? "Host" : "Alice");
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
    Service();
    Drain();
    h10 = R("H").Append(room_id, "|H10", 10);
    Service();
    Drain();
    a20 = R("A").Append(room_id, "|A20", 20);
    Service();
    Drain();
    CHECK(R("A").Peer(room_id, "H").HasDelivered(a20));
    R("B").Expect(room_id);
  }
  MeshReplica& R(std::string const& uid) const { return *replicas.at(uid); }
  void Restart(std::string const& uid) {
    replicas[uid].reset();
    auto observer = [this](auto const& source, auto const& destination,
                           auto const& message, auto const& bytes) {
      if (message.kind == SharedSyncKind::Ack) {
        CheckAck(source, destination, message);
      }
      sent.push_back({source, destination, message, bytes});
    };
    replicas[uid] =
        std::make_unique<MeshReplica>(network, uid, disks.at(uid), observer);
  }
  void Admit(std::uint64_t timestamp = 30) {
    auto& host = R("H");
    auto participant = MakeParticipant(*host.domain, "B", "Bob");
    auto event = MeshJoinEvent::ptr::Create(ae::CreateWith{*host.domain});
    event->participant = participant;
    join_id = event.id();
    bool const admitted =
        host.runtime->AdmitPeer(room_id, event, "B", timestamp);
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
  std::optional<MemoryLinkNetwork::Packet> Find(
      SharedSyncKind kind, std::string const& source,
      std::string const& destination, ae::ObjId event_id = {},
      std::optional<SharedSyncKind> ack_kind = {}) const {
    for (auto const& packet : network.PendingPackets()) {
      auto const message = Decode(packet);
      if (message.kind == kind && packet.source == source &&
          packet.destination == destination &&
          (!event_id.is_valid() || message.event_id == event_id) &&
          (!ack_kind.has_value() || message.acknowledged_kind == *ack_kind)) {
        return packet;
      }
    }
    return {};
  }
  MemoryLinkNetwork::Packet Packet(
      SharedSyncKind kind, std::string const& source,
      std::string const& destination, ae::ObjId event_id = {},
      std::optional<SharedSyncKind> ack_kind = {}) const {
    auto packet = Find(kind, source, destination, event_id, ack_kind);
    CHECK(packet.has_value());
    return *packet;
  }
  void Deliver(MemoryLinkNetwork::Packet const& packet) {
    bool const delivered = network.Deliver(packet.id);
    CHECK(delivered);
  }
  void Drop(MemoryLinkNetwork::Packet const& packet) {
    bool const dropped = network.Drop(packet.id);
    CHECK(dropped);
  }
  void Drain(bool reverse = false) {
    for (unsigned turn = 0; turn != 2000; ++turn) {
      auto packets = network.PendingPackets();
      if (reverse) {
        std::reverse(packets.begin(), packets.end());
      }
      bool progress = false;
      for (auto const& packet : packets) {
        if (network.Deliver(packet.id)) {
          progress = true;
          break;
        }
      }
      if (!progress) {
        return;
      }
    }
    CHECK(false);
  }
  bool Pending() const {
    if (!network.PendingPackets().empty()) {
      return true;
    }
    for (auto const& [_, replica] : replicas) {
      for (auto const& room : replica->app->rooms) {
        for (auto const& peer : room->local_state->peers) {
          if (!peer->pending_packets.empty()) {
            return true;
          }
        }
      }
    }
    return false;
  }
  void Settle(bool reverse = false) {
    for (unsigned turn = 0; turn != 12; ++turn) {
      Service(turn == 0 ? 0 : kRetry);
      Drain(reverse);
      if (!Pending()) {
        return;
      }
    }
    CHECK(false);
  }
  std::size_t Count(std::string const& source, std::string const& destination,
                    ae::ObjId event_id) const {
    return std::count_if(sent.begin(), sent.end(), [&](auto const& frame) {
      return frame.source == source && frame.destination == destination &&
             frame.message.kind == SharedSyncKind::Event &&
             frame.message.event_id == event_id;
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
        CHECK(actual[i].id == reference[i].id &&
              actual[i].timestamp == reference[i].timestamp &&
              actual[i].class_id == reference[i].class_id &&
              actual[i].payload == reference[i].payload);
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
    auto original =
        std::find_if(sent.begin(), sent.end(), [&](auto const& frame) {
          return frame.source == destination && frame.destination == source &&
                 frame.message.node_id == ack.node_id &&
                 frame.message.packet_id == ack.packet_id &&
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
    if (ack.minor >= 2 && ack.acknowledged_kind == SharedSyncKind::Event) {
      auto const prefix = room->local_state->ReceivedPrefix(destination);
      CHECK(ack.received_prefix == prefix);
      CHECK(ack.stamp == original->message.stamp);
      for (std::uint64_t index = 1; index <= prefix.index; ++index) {
        auto const* stamp =
            room->local_state->FindStreamIndex(destination, index);
        CHECK(stamp && stamp->timestamp_us <= prefix.timestamp_us);
        auto const record = std::find_if(
            room->journal.begin(), room->journal.end(), [&](auto const& value) {
              return value.event.id() == stamp->event_id;
            });
        if (record == room->journal.end()) {
          CHECK(stamp->timestamp_us <= room->local_state->checkpoint_us);
        } else {
          CHECK(record->order.timestamp_us == stamp->timestamp_us);
        }
      }
    }
    if (ack.acknowledged_kind == SharedSyncKind::Progress) {
      auto const& report = original->message.progress;
      auto const* saved = room->local_state->FindProgress(destination);
      CHECK(saved && saved->author_uid == destination);
      CHECK(saved->members == report.members);
      CHECK(saved->through_timestamp_us >= report.through_timestamp_us);
      for (auto const& cursor : report.received) {
        auto const confirmed =
            std::find_if(saved->received.begin(), saved->received.end(),
                         [&](auto const& value) {
                           return value.author_uid == cursor.author_uid;
                         });
        CHECK(confirmed != saved->received.end() &&
              confirmed->index >= cursor.index);
        CHECK(room->local_state->ReceivedPrefix(cursor.author_uid).index >=
              cursor.index);
        if (cursor.index != 0) {
          auto const* stamp = room->local_state->FindStreamIndex(
              cursor.author_uid, cursor.index);
          CHECK(stamp && stamp->timestamp_us == cursor.timestamp_us);
        }
      }
    } else if (ack.acknowledged_kind == SharedSyncKind::CoverageReceipt) {
      auto const& plan = original->message.admission;
      auto peer = std::find_if(
          room->local_state->peers.begin(), room->local_state->peers.end(),
          [&](auto const& value) {
            return value->link->endpoint_uid == plan.joining_endpoint;
          });
      CHECK(peer != room->local_state->peers.end());
      CHECK((*peer)->coverage_confirmed && (*peer)->admission == plan);
      for (auto id : plan.covered_events) {
        CHECK((*peer)->HasDelivered(id));
      }
    } else {
      auto const actual = Observe(domain, *room);
      ae::RamDomainStorage expected_storage;
      expected_storage.state = original->message.graph.layers;
      ae::Domain expected_domain{expected_storage};
      ae::DomainGraph expected_graph{
          &expected_domain, ae::GraphSerializationScope::NetworkShared};
      auto expected_root =
          expected_graph.LoadRoot(original->message.graph.root_id);
      CHECK(expected_root);
      std::vector<MeshObserved> expected;
      if (ack.acknowledged_kind == SharedSyncKind::InitialState) {
        CHECK(room->local_state->checkpoint_us >=
              original->message.checkpoint_us);
        for (auto const& stamp : original->message.stream_events) {
          auto const* stored =
              room->local_state->FindStreamEvent(stamp.event_id);
          CHECK(stored && *stored == stamp);
        }
        CHECK(expected_root->GetClassId() == MeshRoom::kClassId);
        auto* initial_room = expected_root.as<MeshRoom>();
        CHECK(initial_room);
        expected = Observe(expected_domain, *initial_room,
                           original->message.checkpoint_us);
      } else {
        expected.push_back(Describe(expected_domain, original->message.event_id,
                                    original->message.timestamp_us));
      }
      for (auto const& item : expected) {
        auto found = std::find_if(
            actual.begin(), actual.end(),
            [&](auto const& value) { return value.id == item.id; });
        if (found == actual.end()) {
          auto const* stamp = room->local_state->FindStreamEvent(item.id);
          CHECK(stamp && stamp->timestamp_us == item.timestamp &&
                stamp->timestamp_us <= room->local_state->checkpoint_us);
        } else {
          CHECK(found->timestamp == item.timestamp &&
                found->class_id == item.class_id &&
                found->payload == item.payload);
        }
      }
    }
    ++ack_checks;
  }
};

void TestOverlappingInitialAdmissionsDoNotNotifySnapshotCoveredPeer() {
  Mesh mesh{false};
  auto& host = mesh.R("H");
  auto room = MeshRoom::ptr::Create(ae::CreateWith{*host.domain});
  room->text = "room:";
  auto owner = MakeParticipant(*host.domain, "H", "Host");
  room->participants = {owner};
  room->links = {owner->link};
  SharedSyncRuntime::InitializeLocalState(*room);
  InitializeRuntimeNode(*room);
  mesh.room_id = room.id();
  host.app->Attach(room);
  host.runtime->RegisterNode(room);
  mesh.R("A").Expect(room.id());
  mesh.R("B").Expect(room.id());
  auto join_a = MeshJoinEvent::ptr::Create(ae::CreateWith{*host.domain});
  join_a->participant = MakeParticipant(*host.domain, "A", "Alice");
  auto const first_join = join_a.id();
  bool const admitted = host.runtime->AdmitPeer(room.id(), join_a, "A", 10);
  CHECK(admitted);
  // B's frozen initial state includes A's Join before A's initial ACK arrives.
  mesh.Admit(20);
  mesh.Settle();
  CHECK(!mesh.Pending());
  CHECK(mesh.R("H").Peer(room.id(), "A").initial_complete);
  CHECK(mesh.R("H").Peer(room.id(), "B").initial_complete);
  CHECK(mesh.R("A").Peer(room.id(), "B").coverage_confirmed);
  for (auto const& frame : mesh.sent) {
    CHECK(!(frame.destination == "B" &&
            frame.message.kind == SharedSyncKind::CoverageReceipt &&
            frame.message.event_id == first_join));
  }
  auto const text = mesh.R("B").Append(room.id(), "|after admission", 30);
  mesh.Settle();
  CHECK(mesh.R("B").Peer(room.id(), "H").HasDelivered(text));
  CHECK(mesh.R("B").Peer(room.id(), "A").HasDelivered(text));
}

void TestJoinFirstImmediateDirectDelivery() {
  Mesh mesh;
  mesh.Admit();
  mesh.Service();
  auto const snapshot = mesh.Packet(SharedSyncKind::InitialState, "H", "B");
  mesh.Deliver(mesh.Packet(SharedSyncKind::Event, "H", "A", mesh.join_id));
  CHECK(mesh.R("A").Room(mesh.room_id).participants.size() == 3);
  auto const& assigned = mesh.R("A").Peer(mesh.room_id, "B");
  CHECK(assigned.IsCoveredByAdmission(mesh.a20));
  CHECK(!assigned.coverage_confirmed);
  CHECK(!assigned.HasDelivered(mesh.a20));  // Assignment is not an ACK.
  CHECK(!mesh.Find(SharedSyncKind::CoverageReceipt, "H", "A").has_value());
  CHECK(!mesh.R("H")
             .Peer(mesh.room_id, "A")
             .FindPending(mesh.Decode(snapshot).packet_id));
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
  mesh.Settle(true);
  mesh.Equal(6);
  CHECK(mesh.Count("B", "H", b50) >= 1 && mesh.Count("B", "A", b50) >= 1);
  CHECK(mesh.Count("H", "A", h60) >= 1 && mesh.Count("H", "B", h60) >= 1);
  CHECK(mesh.Count("H", "A", b50) == 0 && mesh.Count("H", "B", a40) == 0);
}

void TestSnapshotAndReceiptBeforeJoin() {
  Mesh mesh;
  mesh.Admit();
  mesh.Service();
  auto const join = mesh.Packet(SharedSyncKind::Event, "H", "A", mesh.join_id);
  mesh.Deliver(mesh.Packet(SharedSyncKind::InitialState, "H", "B"));
  mesh.Deliver(mesh.Packet(SharedSyncKind::Ack, "B", "H", {},
                           SharedSyncKind::InitialState));
  mesh.Service();
  auto const notice = mesh.Packet(SharedSyncKind::CoverageReceipt, "H", "A");
  auto const b50 = mesh.R("B").Append(mesh.room_id, "|B50", 50);
  mesh.Service();
  mesh.Deliver(mesh.Packet(SharedSyncKind::Event, "B", "A", b50));
  CHECK(mesh.R("A").Room(mesh.room_id).participants.size() == 2);
  CHECK(!mesh.Find(SharedSyncKind::Ack, "A", "B", b50).has_value());
  mesh.Deliver(notice);
  CHECK(!mesh.Find(SharedSyncKind::Ack, "A", "H", {},
                   SharedSyncKind::CoverageReceipt)
             .has_value());
  CHECK(mesh.R("H").Peer(mesh.room_id, "A").FindPending(mesh.join_id));
  mesh.Deliver(join);
  mesh.Settle(true);
  mesh.Equal(4);
}

void TestMissingOwnHistoryUsesOriginalFrameAfterHostAck() {
  Mesh mesh;
  auto const a25 = mesh.R("A").Append(mesh.room_id, "|A25", 25);
  mesh.Service();
  auto const original = mesh.Packet(SharedSyncKind::Event, "A", "H", a25);
  mesh.Admit();
  mesh.Service();  // The frozen snapshot does not contain A25.
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
  mesh.Settle(true);
  mesh.Equal(4);
  CHECK(mesh.Count("A", "B", a25) >= 2);
}

void TestSnapshotReceiptDoesNotAcknowledgeAnotherRecipient() {
  Mesh mesh;
  auto const h25 = mesh.R("H").Append(mesh.room_id, "|H25", 25);
  mesh.Service();
  auto const old_to_a = mesh.Packet(SharedSyncKind::Event, "H", "A", h25);
  mesh.Admit();
  mesh.Service();
  mesh.Deliver(mesh.Packet(SharedSyncKind::InitialState, "H", "B"));
  mesh.Deliver(mesh.Packet(SharedSyncKind::Ack, "B", "H", {},
                           SharedSyncKind::InitialState));
  auto const* pending = mesh.R("H").Peer(mesh.room_id, "A").FindPending(h25);
  CHECK(pending && pending->bytes == old_to_a.bytes);
  mesh.Drop(old_to_a);
  mesh.Deliver(mesh.Packet(SharedSyncKind::Event, "H", "A", mesh.join_id));
  mesh.Settle(true);
  mesh.Equal(4);
  CHECK(mesh.Count("H", "A", h25) >= 2);
  CHECK(mesh.Count("H", "B", h25) == 0);
}

void TestLostControlPacketsAndRestartEveryPhase() {
  Mesh mesh;
  mesh.Admit();
  mesh.Service();
  auto const first_join =
      mesh.Packet(SharedSyncKind::Event, "H", "A", mesh.join_id);
  auto const first_snapshot =
      mesh.Packet(SharedSyncKind::InitialState, "H", "B");
  mesh.Drop(first_join);
  mesh.Drop(first_snapshot);
  mesh.Restart("H");
  mesh.Restart("A");
  mesh.Restart("B");
  mesh.Service(kRetry);
  auto const join = mesh.Packet(SharedSyncKind::Event, "H", "A", mesh.join_id);
  auto const snapshot = mesh.Packet(SharedSyncKind::InitialState, "H", "B");
  CHECK(join.bytes == first_join.bytes &&
        snapshot.bytes == first_snapshot.bytes);
  mesh.Deliver(join);
  mesh.Drop(mesh.Packet(SharedSyncKind::Ack, "A", "H", mesh.join_id));
  mesh.Deliver(snapshot);
  mesh.Drop(mesh.Packet(SharedSyncKind::Ack, "B", "H", {},
                        SharedSyncKind::InitialState));
  CHECK(!mesh.R("H").Peer(mesh.room_id, "B").initial_complete);
  mesh.Restart("A");
  mesh.Restart("B");
  mesh.Restart("H");
  mesh.Service(kRetry);
  auto const retry_snapshot =
      mesh.Packet(SharedSyncKind::InitialState, "H", "B");
  CHECK(retry_snapshot.bytes == first_snapshot.bytes);
  mesh.Deliver(retry_snapshot);
  mesh.Deliver(mesh.Packet(SharedSyncKind::Ack, "B", "H", {},
                           SharedSyncKind::InitialState));
  mesh.Restart("H");  // Receipt obligation must already be durable.
  mesh.Service();
  auto const notice = mesh.Packet(SharedSyncKind::CoverageReceipt, "H", "A");
  mesh.Drop(notice);
  mesh.Restart("H");
  mesh.Restart("A");
  mesh.Service(kRetry);
  auto const retry_notice =
      mesh.Packet(SharedSyncKind::CoverageReceipt, "H", "A");
  CHECK(retry_notice.bytes == notice.bytes);
  mesh.Deliver(retry_notice);
  auto const receipt_ack = mesh.Packet(SharedSyncKind::Ack, "A", "H", {},
                                       SharedSyncKind::CoverageReceipt);
  mesh.Drop(receipt_ack);
  CHECK(mesh.R("A").Peer(mesh.room_id, "B").coverage_confirmed);
  mesh.Restart("A");
  mesh.Restart("H");
  mesh.Service(kRetry);
  CHECK(mesh.Packet(SharedSyncKind::CoverageReceipt, "H", "A").bytes ==
        notice.bytes);
  mesh.Settle(true);
  mesh.Equal(3);
  auto const a40 = mesh.R("A").Append(mesh.room_id, "|A40", 40);
  mesh.Settle();
  mesh.Equal(4);
  CHECK(mesh.Count("A", "B", a40) == 1);
  CHECK(mesh.ack_checks >= 10);
}

void TestOfflineAndAllThreeRestartWithDirectPending() {
  Mesh mesh;
  mesh.Admit();
  mesh.Settle(true);
  mesh.Equal(3);
  mesh.network.SetAvailability("B", LinkAvailability::Offline);
  auto const a40 = mesh.R("A").Append(mesh.room_id, "|A40", 40);
  auto const h50 = mesh.R("H").Append(mesh.room_id, "|H50", 50);
  mesh.Service();
  mesh.Drain(true);
  CHECK(mesh.R("H").Room(mesh.room_id).text ==
        mesh.R("A").Room(mesh.room_id).text);
  CHECK(mesh.R("B").Room(mesh.room_id).journal.size() == 3);
  CHECK(mesh.Count("A", "B", a40) == 0 && mesh.Count("H", "B", h50) == 0);
  mesh.Service(100 * kRetry);
  mesh.Drain();
  CHECK(mesh.Count("A", "B", a40) == 0 && mesh.Count("H", "B", h50) == 0);
  mesh.Restart("H");
  mesh.Restart("A");
  mesh.Restart("B");
  mesh.network.SetAvailability("B", LinkAvailability::Offline);
  mesh.Service(kRetry);
  mesh.Drain();
  CHECK(mesh.Count("A", "B", a40) == 0 && mesh.Count("H", "B", h50) == 0);
  mesh.network.SetAvailability("B", LinkAvailability::Online);
  mesh.Service();
  auto const packet = mesh.Packet(SharedSyncKind::Event, "A", "B", a40);
  auto const duplicate = mesh.network.Duplicate(packet.id);
  CHECK(duplicate != 0);
  mesh.Settle(true);
  mesh.Equal(5);
  auto const b60 = mesh.R("B").Append(mesh.room_id, "|B60", 60);
  mesh.Settle(true);
  mesh.Equal(6);
  CHECK(mesh.Count("B", "A", b60) == 1 && mesh.Count("B", "H", b60) == 1);
  auto const a70 = mesh.R("A").Append(mesh.room_id, "|A70", 70);
  auto const b80 = mesh.R("B").Append(mesh.room_id, "|B80", 80);
  auto const h90 = mesh.R("H").Append(mesh.room_id, "|H90", 90);
  CHECK(mesh.network.PendingPackets().empty());
  mesh.Restart("H");
  mesh.Restart("A");
  mesh.Restart("B");
  mesh.Settle(true);
  mesh.Equal(9);
  CHECK(mesh.Count("A", "H", a70) == 1 && mesh.Count("A", "B", a70) == 1);
  CHECK(mesh.Count("B", "H", b80) == 1 && mesh.Count("B", "A", b80) == 1);
  CHECK(mesh.Count("H", "A", h90) == 1 && mesh.Count("H", "B", h90) == 1);
  CHECK(mesh.Count("H", "B", a70) == 0 && mesh.Count("A", "H", b80) == 0);
}

void TestHostOfflineLeavesDirectClientsWorking() {
  Mesh mesh;
  mesh.Admit();
  mesh.Settle(true);
  mesh.Equal(3);
  mesh.network.SetAvailability("H", LinkAvailability::Offline);
  auto const a40 = mesh.R("A").Append(mesh.room_id, "|A40", 40);
  auto const b50 = mesh.R("B").Append(mesh.room_id, "|B50", 50);
  mesh.Service();
  mesh.Drain(true);
  CHECK(mesh.R("A").Room(mesh.room_id).text ==
        mesh.R("B").Room(mesh.room_id).text);
  CHECK(mesh.R("A").Room(mesh.room_id).journal.size() == 5);
  CHECK(mesh.R("H").Room(mesh.room_id).journal.size() == 3);
  CHECK(mesh.Count("A", "B", a40) == 1 && mesh.Count("B", "A", b50) == 1);
  CHECK(mesh.Count("A", "H", a40) == 0 && mesh.Count("B", "H", b50) == 0);
  mesh.Service(10 * kRetry);
  mesh.Drain();
  CHECK(mesh.Count("A", "H", a40) == 0 && mesh.Count("B", "H", b50) == 0);
  mesh.Restart("H");
  mesh.network.SetAvailability("H", LinkAvailability::Offline);
  mesh.Service(kRetry);
  mesh.Drain();
  CHECK(mesh.Count("A", "H", a40) == 0 && mesh.Count("B", "H", b50) == 0);
  mesh.network.SetAvailability("H", LinkAvailability::Online);
  mesh.Settle(true);
  mesh.Equal(5);
  CHECK(mesh.Count("A", "H", a40) == 1 && mesh.Count("B", "H", b50) == 1);
  CHECK(mesh.Count("H", "A", a40) == 0 && mesh.Count("H", "B", a40) == 0);
  CHECK(mesh.Count("H", "A", b50) == 0 && mesh.Count("H", "B", b50) == 0);
  auto const h60 = mesh.R("H").Append(mesh.room_id, "|H60", 60);
  mesh.Settle();
  mesh.Equal(6);
  CHECK(mesh.Count("H", "A", h60) == 1 && mesh.Count("H", "B", h60) == 1);
}

void TestLegacyNativeEventAfterAdmission() {
  Mesh mesh;
  mesh.Admit();
  mesh.Settle(true);
  mesh.Equal(3);
  auto const a40 = mesh.R("A").AppendLegacy(mesh.room_id, "|A40", 40);
  mesh.Service();
  auto const current = mesh.Packet(SharedSyncKind::Event, "A", "B", a40);
  auto legacy = mesh.Decode(current);
  CHECK(!legacy.admission.is_valid());
  legacy.minor = 0;
  std::vector<std::uint8_t> bytes;
  auto unsupported = legacy;
  unsupported.admission = mesh.R("A").Peer(mesh.room_id, "B").admission;
  bool const admission_downgraded =
      SerializeSharedSyncMessage(unsupported, bytes);
  CHECK(!admission_downgraded);
  unsupported.admission = {};
  unsupported.kind = SharedSyncKind::CoverageReceipt;
  bool const receipt_downgraded =
      SerializeSharedSyncMessage(unsupported, bytes);
  CHECK(!receipt_downgraded);
  unsupported.kind = SharedSyncKind::Ack;
  unsupported.acknowledged_kind = SharedSyncKind::CoverageReceipt;
  bool const receipt_ack_downgraded =
      SerializeSharedSyncMessage(unsupported, bytes);
  CHECK(!receipt_ack_downgraded);
  bool const encoded = SerializeSharedSyncMessage(legacy, bytes);
  CHECK(encoded);
  SharedSyncMessage decoded;
  bool const parsed = DeserializeSharedSyncMessage(bytes, decoded);
  CHECK(parsed && decoded.major == 1 && decoded.minor == 0);
  CHECK(decoded.event_id == a40 && decoded.graph.layers == legacy.graph.layers);
  auto const packet = current;
  CHECK(mesh.Decode(packet).minor == 0);
  mesh.Deliver(packet);
  auto const ack = mesh.Packet(SharedSyncKind::Ack, "B", "A", a40);
  CHECK(mesh.Decode(ack).minor == 0);
  mesh.Deliver(ack);
  CHECK(mesh.R("A").Peer(mesh.room_id, "B").HasDelivered(a40));
  CHECK(mesh.R("A").Peer(mesh.room_id, "B").FindPending(a40) == nullptr);
  mesh.Settle(true);
  mesh.Equal(4);

  auto const a50 = mesh.R("A").AppendLegacy(mesh.room_id, "|A50", 50, 1);
  mesh.Service();
  auto const v1 = mesh.Packet(SharedSyncKind::Event, "A", "B", a50);
  CHECK(mesh.Decode(v1).minor == 1 && !mesh.Decode(v1).stamp.is_valid());
  mesh.Deliver(v1);
  auto const v1_ack = mesh.Packet(SharedSyncKind::Ack, "B", "A", a50);
  CHECK(mesh.Decode(v1_ack).minor == 1);
  mesh.Deliver(v1_ack);
  CHECK(mesh.R("A").Peer(mesh.room_id, "B").HasDelivered(a50));
  CHECK(!mesh.R("A").Peer(mesh.room_id, "B").FindPending(a50));
  mesh.Settle(true);
  mesh.Equal(5);

  // The previous indexed tuple preserves all Event fields; only the newer
  // progress/checkpoint extensions are absent, so this is a lossless encoding.
  auto const a60 = mesh.R("A").Append(mesh.room_id, "|A60", 60);
  mesh.Service();
  auto const v3 = mesh.Packet(SharedSyncKind::Event, "A", "B", a60);
  auto indexed_v2 = mesh.Decode(v3);
  indexed_v2.minor = 2;
  std::vector<std::uint8_t> v2_bytes;
  bool const encoded_v2 = SerializeSharedSyncMessage(indexed_v2, v2_bytes);
  CHECK(encoded_v2);
  mesh.Drop(v3);
  bool const replaced = mesh.R("A").endpoint->Send("B", v2_bytes);
  CHECK(replaced);
  auto const v2 = mesh.Packet(SharedSyncKind::Event, "A", "B", a60);
  CHECK(mesh.Decode(v2).minor == 2 && mesh.Decode(v2).stamp.index == 2);
  mesh.Deliver(v2);
  auto const v2_ack = mesh.Packet(SharedSyncKind::Ack, "B", "A", a60);
  CHECK(mesh.Decode(v2_ack).minor == 2);
  CHECK(mesh.Decode(v2_ack).received_prefix ==
        (SharedStreamCursor{"A", 2, 60}));
  mesh.Deliver(v2_ack);
  CHECK(!mesh.R("A").Peer(mesh.room_id, "B").FindPending(a60));
  mesh.Settle(true);
  mesh.Equal(6);
}

SharedStreamCursor Confirmed(MeshReplica const& replica, ae::ObjId room_id,
                             std::string const& destination,
                             std::string const& author) {
  for (auto const& cursor :
       replica.Peer(room_id, destination).acknowledged_streams) {
    if (cursor.author_uid == author) {
      return cursor;
    }
  }
  return {author, 0, 0};
}

void TestContinuousAckGapLossAndSparseRestart() {
  Mesh mesh;
  mesh.Admit();
  mesh.Settle(true);
  mesh.Equal(3);
  auto const a35 = mesh.R("A").Append(mesh.room_id, "|A35", 35);
  auto const a40 = mesh.R("A").Append(mesh.room_id, "|A40", 40);
  mesh.Service();
  auto const first_b = mesh.Packet(SharedSyncKind::Event, "A", "B", a35);
  auto const later_b = mesh.Packet(SharedSyncKind::Event, "A", "B", a40);
  auto const first_stamp = mesh.Decode(first_b).stamp;
  auto const later_stamp = mesh.Decode(later_b).stamp;
  CHECK(first_stamp.author_uid == "A" && first_stamp.index == 2);
  CHECK(later_stamp.author_uid == "A" && later_stamp.index == 3);
  CHECK(first_stamp.event_id == a35 && later_stamp.event_id == a40);
  CHECK(first_stamp.timestamp_us == 35 && later_stamp.timestamp_us == 40);
  auto const duplicate = mesh.network.Duplicate(later_b.id);
  CHECK(duplicate != 0);
  mesh.Deliver(later_b);
  auto hole_ack = mesh.Packet(SharedSyncKind::Ack, "B", "A", a40);
  CHECK(mesh.Decode(hole_ack).received_prefix ==
        (SharedStreamCursor{"A", 1, 20}));
  mesh.Deliver(hole_ack);
  CHECK(mesh.R("A").Peer(mesh.room_id, "B").FindPending(a35));
  CHECK(mesh.R("A").Peer(mesh.room_id, "B").FindPending(a40));
  CHECK(!mesh.R("A").Peer(mesh.room_id, "B").HasDelivered(a40));

  // The same source can have a different confirmed prefix at another peer.
  mesh.Deliver(mesh.Packet(SharedSyncKind::Event, "A", "H", a35));
  auto const h_ack = mesh.Packet(SharedSyncKind::Ack, "H", "A", a35);
  CHECK(mesh.Decode(h_ack).received_prefix == (SharedStreamCursor{"A", 2, 35}));
  mesh.Deliver(h_ack);
  CHECK(!mesh.R("A").Peer(mesh.room_id, "H").FindPending(a35));
  CHECK(mesh.R("A").Peer(mesh.room_id, "B").FindPending(a35));
  CHECK(Confirmed(mesh.R("A"), mesh.room_id, "H", "A").index == 2);

  mesh.Restart("B");
  auto const& sparse = *mesh.R("B").Room(mesh.room_id).local_state;
  CHECK(sparse.FindStreamIndex("A", 3));
  CHECK(!sparse.FindStreamIndex("A", 2));
  CHECK(sparse.ReceivedPrefix("A") == (SharedStreamCursor{"A", 1, 20}));
  mesh.Restart("A");
  CHECK(mesh.R("A").Peer(mesh.room_id, "B").FindPending(a40)->bytes ==
        later_b.bytes);
  auto const duplicate_packet =
      mesh.Packet(SharedSyncKind::Event, "A", "B", a40);
  mesh.Deliver(duplicate_packet);
  hole_ack = mesh.Packet(SharedSyncKind::Ack, "B", "A", a40);
  CHECK(mesh.Decode(hole_ack).received_prefix.index == 1);
  mesh.Deliver(hole_ack);
  CHECK(mesh.R("A").Peer(mesh.room_id, "B").FindPending(a40));

  mesh.Deliver(first_b);
  auto const filled_ack = mesh.Packet(SharedSyncKind::Ack, "B", "A", a35);
  CHECK(mesh.Decode(filled_ack).received_prefix ==
        (SharedStreamCursor{"A", 3, 40}));
  mesh.Drop(filled_ack);
  mesh.Restart("B");
  mesh.Service(kRetry);
  auto const retry = mesh.Packet(SharedSyncKind::Event, "A", "B", a35);
  CHECK(retry.bytes == first_b.bytes);
  mesh.Deliver(retry);
  auto const replay_ack = mesh.Packet(SharedSyncKind::Ack, "B", "A", a35);
  CHECK(mesh.Decode(replay_ack).received_prefix.index == 3);
  mesh.Deliver(replay_ack);
  CHECK(!mesh.R("A").Peer(mesh.room_id, "B").FindPending(a35));
  CHECK(!mesh.R("A").Peer(mesh.room_id, "B").FindPending(a40));
  CHECK(mesh.R("A").Peer(mesh.room_id, "B").HasDelivered(a35));
  CHECK(mesh.R("A").Peer(mesh.room_id, "B").HasDelivered(a40));
  CHECK(Confirmed(mesh.R("A"), mesh.room_id, "B", "A") ==
        (SharedStreamCursor{"A", 3, 40}));
  mesh.Settle(true);
  mesh.Equal(5);
  mesh.Restart("A");
  auto const a50 = mesh.R("A").Append(mesh.room_id, "|A50", 50);
  auto const* next =
      mesh.R("A").Room(mesh.room_id).local_state->FindStreamEvent(a50);
  CHECK(next && next->author_uid == "A" && next->index == 4 &&
        next->timestamp_us == 50);
  mesh.Settle(true);
  mesh.Equal(6);
}

void TestSnapshotPreservesSparseStreamCoverage() {
  Mesh mesh;
  auto const a25 = mesh.R("A").Append(mesh.room_id, "|A25", 25);
  auto const a27 = mesh.R("A").Append(mesh.room_id, "|A27", 27);
  mesh.Service();
  mesh.Deliver(mesh.Packet(SharedSyncKind::Event, "A", "H", a27));
  auto const sparse_ack = mesh.Packet(SharedSyncKind::Ack, "H", "A", a27);
  CHECK(mesh.Decode(sparse_ack).received_prefix.index == 1);
  mesh.Deliver(sparse_ack);
  CHECK(mesh.R("A").Peer(mesh.room_id, "H").FindPending(a27));
  mesh.Admit();
  mesh.Service();
  auto const snapshot = mesh.Packet(SharedSyncKind::InitialState, "H", "B");
  auto const message = mesh.Decode(snapshot);
  auto const snapshot_stamp =
      std::find_if(message.stream_events.begin(), message.stream_events.end(),
                   [&](auto const& value) { return value.event_id == a27; });
  CHECK(snapshot_stamp != message.stream_events.end() &&
        snapshot_stamp->index == 3);
  CHECK(std::none_of(message.stream_events.begin(), message.stream_events.end(),
                     [&](auto const& value) { return value.event_id == a25; }));
  mesh.Deliver(snapshot);
  auto const& ledger = *mesh.R("B").Room(mesh.room_id).local_state;
  CHECK(ledger.FindStreamIndex("A", 3));
  CHECK(!ledger.FindStreamIndex("A", 2));
  CHECK(ledger.ReceivedPrefix("A").index == 1);
  mesh.Deliver(mesh.Packet(SharedSyncKind::Ack, "B", "H", {},
                           SharedSyncKind::InitialState));
  mesh.Deliver(mesh.Packet(SharedSyncKind::Event, "H", "A", mesh.join_id));
  mesh.Service();
  CHECK(mesh.Count("A", "B", a25) == 1);
  CHECK(mesh.Count("A", "B", a27) == 0);
  mesh.Deliver(mesh.Packet(SharedSyncKind::Event, "A", "B", a25));
  auto const filled = mesh.Packet(SharedSyncKind::Ack, "B", "A", a25);
  CHECK(mesh.Decode(filled).received_prefix ==
        (SharedStreamCursor{"A", 3, 27}));
  mesh.Deliver(filled);
  mesh.Settle(true);
  mesh.Equal(5);
  CHECK(mesh.Count("A", "B", a27) == 0);
}

void TestInvalidPrefixCannotCancelPending() {
  Mesh mesh;
  mesh.Admit();
  mesh.Settle(true);
  mesh.Equal(3);
  auto const a40 = mesh.R("A").Append(mesh.room_id, "|A40", 40);
  mesh.Service();
  auto const data = mesh.Packet(SharedSyncKind::Event, "A", "B", a40);
  auto downgrade = mesh.Decode(data);
  std::vector<std::uint8_t> downgraded;
  downgrade.minor = 0;
  bool const dropped_index_v0 =
      SerializeSharedSyncMessage(downgrade, downgraded);
  CHECK(!dropped_index_v0);
  downgrade.minor = 1;
  bool const dropped_index_v1 =
      SerializeSharedSyncMessage(downgrade, downgraded);
  CHECK(!dropped_index_v1);
  mesh.Deliver(data);
  auto const good_packet = mesh.Packet(SharedSyncKind::Ack, "B", "A", a40);
  auto const good = mesh.Decode(good_packet);
  CHECK(good.received_prefix == (SharedStreamCursor{"A", 2, 40}));
  mesh.Drop(good_packet);
  auto inject = [&](SharedSyncMessage const& bad) {
    std::vector<std::uint8_t> bytes;
    bool const encoded = SerializeSharedSyncMessage(bad, bytes);
    CHECK(encoded);
    auto const rejected = mesh.R("A").runtime->rejected_messages();
    bool const sent = mesh.R("B").endpoint->Send("A", bytes);
    CHECK(sent);
    // Some mutations intentionally violate the native message shape, so do not
    // route this raw packet through Mesh::Packet's valid-message decoder.
    mesh.Deliver(mesh.network.PendingPackets().back());
    CHECK(mesh.R("A").runtime->rejected_messages() > rejected);
    CHECK(mesh.R("A").Peer(mesh.room_id, "B").FindPending(a40));
    CHECK(!mesh.R("A").Peer(mesh.room_id, "B").HasDelivered(a40));
  };
  auto bad = good;
  bad.received_prefix.index = 3;
  inject(bad);
  bad = good;
  bad.received_prefix.author_uid = "H";
  inject(bad);
  bad = good;
  ++bad.received_prefix.timestamp_us;
  inject(bad);
  bad = good;
  ++bad.stamp.index;
  inject(bad);
  bad = good;
  bad.minor = 1;
  bad.stamp = {};
  bad.received_prefix = {};
  inject(bad);  // A legacy ACK cannot confirm a newer indexed pending packet.
  mesh.Restart("A");
  CHECK(mesh.R("A").Peer(mesh.room_id, "B").FindPending(a40)->bytes ==
        data.bytes);
  mesh.Settle(true);
  mesh.Equal(4);
}

void TestMonotonicAuthorClockAndSnapshotFloorSurviveRestart() {
  Mesh mesh;
  constexpr std::uint64_t future = 1000000000000000000ULL;
  mesh.R("H").Append(mesh.room_id, "|future", future);
  mesh.Service();
  mesh.Drain();
  mesh.Admit(future + 10);
  mesh.Settle(true);
  mesh.Equal(4);
  CHECK(mesh.R("B").Room(mesh.room_id).local_state->timestamp_floor ==
        future + 10);
  auto reject = [&](std::uint64_t timestamp) {
    auto& replica = mesh.R("B");
    auto event = replica.NewText(mesh.room_id, "|rejected");
    auto const journal_size = replica.Room(mesh.room_id).journal.size();
    auto const ledger_size =
        replica.Room(mesh.room_id).local_state->stream_events.size();
    auto const saves = replica.durable.saves;
    bool const committed =
        replica.runtime->CommitLocal(mesh.room_id, event, timestamp);
    CHECK(!committed && !replica.runtime->failed());
    CHECK(replica.Room(mesh.room_id).journal.size() == journal_size);
    CHECK(replica.Room(mesh.room_id).local_state->stream_events.size() ==
          ledger_size);
    CHECK(replica.durable.saves == saves);
    CHECK(!replica.Room(mesh.room_id).local_state->FindStreamEvent(event.id()));
  };
  reject(future + 10);
  reject(future);
  auto const first = mesh.R("B").Append(mesh.room_id, "|auto1", 0);
  auto const first_stamp =
      *mesh.R("B").Room(mesh.room_id).local_state->FindStreamEvent(first);
  CHECK(first_stamp.index == 1 && first_stamp.timestamp_us > future + 10);
  reject(first_stamp.timestamp_us);
  mesh.Settle(true);
  mesh.Equal(5);
  mesh.Restart("B");
  CHECK(mesh.R("B").Room(mesh.room_id).local_state->timestamp_floor ==
        future + 10);
  reject(first_stamp.timestamp_us);
  reject(first_stamp.timestamp_us - 1);
  auto const second = mesh.R("B").Append(mesh.room_id, "|auto2", 0);
  auto const second_stamp =
      *mesh.R("B").Room(mesh.room_id).local_state->FindStreamEvent(second);
  CHECK(second_stamp.index == 2 &&
        second_stamp.timestamp_us > first_stamp.timestamp_us);
  mesh.Settle(true);
  mesh.Equal(6);
}

void TestInboundStreamIdentityAndTimestampConflicts() {
  Mesh mesh;
  mesh.Admit();
  mesh.Settle(true);
  mesh.Equal(3);
  auto const a35 = mesh.R("A").Append(mesh.room_id, "|A35", 35);
  auto const a40 = mesh.R("A").Append(mesh.room_id, "|A40", 40);
  auto const a50 = mesh.R("A").Append(mesh.room_id, "|A50", 50);
  mesh.Service();
  auto const first = mesh.Packet(SharedSyncKind::Event, "A", "B", a35);
  auto const middle = mesh.Packet(SharedSyncKind::Event, "A", "B", a40);
  auto const last = mesh.Packet(SharedSyncKind::Event, "A", "B", a50);
  mesh.Deliver(first);
  mesh.Deliver(mesh.Packet(SharedSyncKind::Ack, "B", "A", a35));
  mesh.Deliver(last);
  auto const gap_ack = mesh.Packet(SharedSyncKind::Ack, "B", "A", a50);
  CHECK(mesh.Decode(gap_ack).received_prefix.index == 2);
  mesh.Deliver(gap_ack);
  auto const& ledger = *mesh.R("B").Room(mesh.room_id).local_state;
  CHECK(ledger.FindStreamIndex("A", 2) && ledger.FindStreamIndex("A", 4));
  CHECK(!ledger.FindStreamIndex("A", 3));
  auto reject = [&](SharedSyncMessage const& bad) {
    auto const saves = mesh.R("B").durable.saves;
    auto const rejections = mesh.R("B").runtime->rejected_messages();
    auto const records = mesh.R("B").Room(mesh.room_id).journal.size();
    auto const stamps =
        mesh.R("B").Room(mesh.room_id).local_state->stream_events.size();
    auto const sends = mesh.sent.size();
    auto const native_storage = mesh.R("B").storage.state;
    std::vector<std::uint8_t> bytes;
    bool const encoded = SerializeSharedSyncMessage(bad, bytes);
    CHECK(encoded);
    SharedSyncMessage shape_checked;
    bool const valid_shape = DeserializeSharedSyncMessage(bytes, shape_checked);
    CHECK(valid_shape);  // Exercise stream validation beyond native framing.
    bool const sent = mesh.R("A").endpoint->Send("B", bytes);
    CHECK(sent);
    mesh.Deliver(mesh.network.PendingPackets().back());
    CHECK(mesh.R("B").runtime->rejected_messages() > rejections);
    CHECK(mesh.R("B").durable.saves == saves && mesh.sent.size() == sends);
    CHECK(mesh.R("B").Room(mesh.room_id).journal.size() == records);
    CHECK(mesh.R("B").Room(mesh.room_id).local_state->stream_events.size() ==
          stamps);
    CHECK(mesh.R("B").storage.state == native_storage);
  };
  auto bad = mesh.Decode(middle);
  bad.stamp.index = 2;  // This index already names A35, not A40.
  reject(bad);
  bad = mesh.Decode(first);
  bad.stamp.index = 5;  // A known Event cannot acquire a new stream position.
  bad.stamp.timestamp_us = bad.timestamp_us = 60;
  reject(bad);
  bad = mesh.Decode(middle);
  bad.stamp.timestamp_us = bad.timestamp_us = 34;  // Below index 2's time 35.
  reject(bad);
  bad.stamp.timestamp_us = bad.timestamp_us = 51;  // Above index 4's time 50.
  reject(bad);
  CHECK(!mesh.R("B").Room(mesh.room_id).local_state->FindStreamEvent(a40));
  mesh.Settle(true);
  mesh.Equal(6);
  CHECK(mesh.R("B").Room(mesh.room_id).local_state->ReceivedPrefix("A").index ==
        4);
}

void TestMalformedSnapshotStreamMetadataDoesNotAttach() {
  Mesh mesh;
  mesh.Admit();
  mesh.Service();
  auto const snapshot = mesh.Packet(SharedSyncKind::InitialState, "H", "B");
  auto const original = mesh.Decode(snapshot);
  CHECK(original.stream_events.size() >= 3);
  auto reject = [&](SharedSyncMessage const& bad) {
    auto const saves = mesh.R("B").durable.saves;
    auto const rejections = mesh.R("B").runtime->rejected_messages();
    auto const sends = mesh.sent.size();
    std::vector<std::uint8_t> bytes;
    bool const encoded = SerializeSharedSyncMessage(bad, bytes);
    CHECK(encoded);
    bool const sent = mesh.R("H").endpoint->Send("B", bytes);
    CHECK(sent);
    mesh.Deliver(mesh.network.PendingPackets().back());
    CHECK(mesh.R("B").runtime->rejected_messages() > rejections);
    CHECK(mesh.R("B").durable.saves == saves && mesh.sent.size() == sends);
    CHECK(mesh.R("B").app->rooms.empty());
    CHECK(!mesh.R("B").runtime->FindNode(mesh.room_id).is_valid());
    CHECK(!mesh.R("B").domain->Find(mesh.room_id));
    CHECK(mesh.R("B").storage.Enumerate(mesh.room_id).empty());
  };
  auto bad = original;
  bad.stream_events.push_back(bad.stream_events.front());  // Duplicate Event.
  reject(bad);
  bad = original;
  auto first =
      std::find_if(bad.stream_events.begin(), bad.stream_events.end(),
                   [](auto const& value) {
                     return value.author_uid == "H" && value.index == 1;
                   });
  auto second =
      std::find_if(bad.stream_events.begin(), bad.stream_events.end(),
                   [](auto const& value) {
                     return value.author_uid == "H" && value.index == 2;
                   });
  CHECK(first != bad.stream_events.end() && second != bad.stream_events.end());
  second->index = first->index;  // Different Event occupying the same index.
  reject(bad);
  bad = original;
  ++bad.stream_events.front().timestamp_us;  // Does not match journal order.
  reject(bad);
  mesh.Settle(true);
  mesh.Equal(3);
  CHECK(mesh.R("B").Room(mesh.room_id).local_state->ReceivedPrefix("H").index ==
        2);
  CHECK(mesh.R("B").Room(mesh.room_id).local_state->ReceivedPrefix("A").index ==
        1);
}

void Publish(Mesh& mesh, std::initializer_list<std::string> authors,
             std::uint64_t through) {
  for (auto const& author : authors) {
    bool const published =
        mesh.R(author).runtime->PublishProgress(mesh.room_id, through);
    CHECK(published && !mesh.R(author).runtime->failed());
  }
}

void CheckColdCheckpoint(Mesh const& mesh, std::string const& uid,
                         std::uint64_t through, std::size_t journal_size,
                         std::string const& text) {
  auto storage = mesh.disks.at(uid).storage;
  ae::Domain domain{storage};
  ae::DomainGraph graph{&domain};
  auto root = graph.LoadRoot(mesh.disks.at(uid).app_id);
  CHECK(root && root->GetClassId() == MeshApplication::kClassId);
  auto* app = root.as<MeshApplication>();
  CHECK(app->rooms.size() == 1 && app->rooms.front().id() == mesh.room_id);
  auto held = domain.Find(mesh.room_id);
  CHECK(held && held->GetClassId() == MeshRoom::kClassId);
  auto* room = held.as<MeshRoom>();
  CHECK(room->text == text && room->journal.size() == journal_size);
  CHECK(room->local_state->checkpoint_us == through);
  CHECK(room->local_state->timestamp_floor >= through);
}

void Compact(Mesh& mesh, std::initializer_list<std::string> replicas,
             std::uint64_t through, std::size_t journal_size,
             std::string const& text) {
  for (auto const& uid : replicas) {
    bool const compacted = mesh.R(uid).runtime->CompactConfirmed(mesh.room_id);
    CHECK(compacted && !mesh.R(uid).runtime->failed());
    auto const& room = mesh.R(uid).Room(mesh.room_id);
    CHECK(room.text == text && room.journal.size() == journal_size);
    CHECK(room.local_state->checkpoint_us == through);
    CHECK(room.IsJournalCompactionBlocked());
    CHECK(room.local_state->journal.empty());
    for (auto const& peer : room.local_state->peers) {
      CHECK(peer->journal.empty());
    }
    CheckColdCheckpoint(mesh, uid, through, journal_size, text);
  }
}

void TestSilentParticipantBlocksCheckpointAndEmptyIntervalAdvances() {
  Mesh mesh;
  mesh.Admit();
  mesh.Settle(true);
  mesh.Equal(3);
  auto const text = mesh.R("H").Room(mesh.room_id).text;
  Publish(mesh, {"H", "A"}, 100);
  mesh.Settle(true);
  auto const saves = mesh.R("H").durable.saves;
  bool const premature = mesh.R("H").runtime->CompactConfirmed(mesh.room_id);
  CHECK(!premature && mesh.R("H").durable.saves == saves);
  CHECK(mesh.R("H").Room(mesh.room_id).journal.size() == 3);
  CHECK(mesh.R("H").Room(mesh.room_id).local_state->checkpoint_us == 0);

  Publish(mesh, {"B"}, 100);
  mesh.Settle(true);
  Compact(mesh, {"H", "A", "B"}, 100, 0, text);
  for (auto const& uid : {"H", "A", "B"}) {
    auto const& state = *mesh.R(uid).Room(mesh.room_id).local_state;
    CHECK(state.local_frames.empty() && state.local_events.empty());
    CHECK(state.stream_events.size() == 3);  // Small dedup identities remain.
    CHECK(state.FindStreamEvent(mesh.h10) && state.FindStreamEvent(mesh.a20));
    auto const count = mesh.R(uid).durable.saves;
    bool const again = mesh.R(uid).runtime->CompactConfirmed(mesh.room_id);
    CHECK(!again && mesh.R(uid).durable.saves == count);
  }
  // No new Events are needed to close another silent interval.
  Publish(mesh, {"H", "A", "B"}, 200);
  mesh.Settle(true);
  Compact(mesh, {"H", "A", "B"}, 200, 0, text);
}

void TestProgressBeforeEventAndStaleReceiptRequireRefresh() {
  Mesh mesh;
  mesh.Admit();
  mesh.Settle(true);
  mesh.Equal(3);
  auto const a40 = mesh.R("A").Append(mesh.room_id, "|A40", 40);
  mesh.Service();
  mesh.Deliver(mesh.Packet(SharedSyncKind::Event, "A", "H", a40));
  mesh.Deliver(mesh.Packet(SharedSyncKind::Ack, "H", "A", a40));
  mesh.Drop(mesh.Packet(SharedSyncKind::Event, "A", "B", a40));
  Publish(mesh, {"H", "A", "B"}, 100);
  mesh.Service();
  auto const early = mesh.Packet(SharedSyncKind::Progress, "A", "B");
  auto const marker_id = mesh.Decode(early).packet_id;
  mesh.Deliver(early);
  CHECK(!mesh.Find(SharedSyncKind::Ack, "B", "A", marker_id,
                   SharedSyncKind::Progress)
             .has_value());
  CHECK(!mesh.R("B").Room(mesh.room_id).local_state->FindProgress("A"));
  mesh.Drain(true);
  bool const before_data = mesh.R("H").runtime->CompactConfirmed(mesh.room_id);
  CHECK(!before_data);
  mesh.Settle(true);
  CHECK(mesh.R("B").Room(mesh.room_id).local_state->ReceivedPrefix("A").index ==
        2);
  // B's old report truthfully said A:1. Receiving A:2 does not rewrite it.
  auto const* stale =
      mesh.R("H").Room(mesh.room_id).local_state->FindProgress("B");
  CHECK(stale);
  auto const receipt =
      std::find_if(stale->received.begin(), stale->received.end(),
                   [](auto const& value) { return value.author_uid == "A"; });
  CHECK(receipt != stale->received.end() && receipt->index == 1);
  bool const before_refresh =
      mesh.R("H").runtime->CompactConfirmed(mesh.room_id);
  CHECK(!before_refresh && mesh.R("H").Room(mesh.room_id).journal.size() == 4);
  Publish(mesh, {"B"}, 100);
  mesh.Settle(true);
  auto const text = mesh.R("H").Room(mesh.room_id).text;
  Compact(mesh, {"H", "A", "B"}, 100, 0, text);
}

void TestProgressLossOfflineAndRestartPreservePromise() {
  Mesh mesh;
  mesh.Admit();
  mesh.Settle(true);
  mesh.Equal(3);
  Publish(mesh, {"H"}, 100);
  mesh.Service();
  auto const first_a = mesh.Packet(SharedSyncKind::Progress, "H", "A");
  auto const first_b = mesh.Packet(SharedSyncKind::Progress, "H", "B");
  auto const marker_id = mesh.Decode(first_a).packet_id;
  CHECK(mesh.Decode(first_b).packet_id == marker_id);
  mesh.Drop(first_a);
  mesh.Deliver(first_b);
  mesh.Drop(mesh.Packet(SharedSyncKind::Ack, "B", "H", marker_id,
                        SharedSyncKind::Progress));
  mesh.network.SetAvailability("B", LinkAvailability::Offline);
  auto count_b = [&] {
    return std::count_if(
        mesh.sent.begin(), mesh.sent.end(), [](auto const& frame) {
          return frame.source == "H" && frame.destination == "B" &&
                 frame.message.kind == SharedSyncKind::Progress;
        });
  };
  auto const sends_b = count_b();
  mesh.Restart("H");
  auto event = mesh.R("H").NewText(mesh.room_id, "|too-early");
  auto const saves = mesh.R("H").durable.saves;
  bool const too_early =
      mesh.R("H").runtime->CommitLocal(mesh.room_id, event, 100);
  CHECK(!too_early && mesh.R("H").durable.saves == saves);
  event.Reset();  // Its room reference must not keep H alive across restart.
  mesh.Service(kRetry);
  CHECK(count_b() == sends_b);
  auto const retry = mesh.Packet(SharedSyncKind::Progress, "H", "A");
  CHECK(retry.bytes == first_a.bytes);
  mesh.Deliver(retry);
  mesh.Drop(mesh.Packet(SharedSyncKind::Ack, "A", "H", marker_id,
                        SharedSyncKind::Progress));
  mesh.Restart("A");
  mesh.Restart("H");
  mesh.Service(kRetry);
  auto const again = mesh.Packet(SharedSyncKind::Progress, "H", "A");
  CHECK(again.bytes == first_a.bytes);
  mesh.Deliver(again);
  mesh.Deliver(mesh.Packet(SharedSyncKind::Ack, "A", "H", marker_id,
                           SharedSyncKind::Progress));
  CHECK(!mesh.R("H").Peer(mesh.room_id, "A").FindPending(marker_id));
  CHECK(!mesh.R("H").Peer(mesh.room_id, "A").HasDelivered(marker_id));
  CHECK(count_b() == sends_b);
  mesh.network.SetAvailability("B", LinkAvailability::Online);
  mesh.Settle(true);
  CHECK(!mesh.R("H").Peer(mesh.room_id, "B").FindPending(marker_id));
  CHECK(!mesh.R("H").Peer(mesh.room_id, "B").HasDelivered(marker_id));
  bool const incomplete = mesh.R("H").runtime->CompactConfirmed(mesh.room_id);
  CHECK(!incomplete);
  Publish(mesh, {"A", "B"}, 100);
  mesh.Settle(true);
  auto const text = mesh.R("H").Room(mesh.room_id).text;
  Compact(mesh, {"H", "A", "B"}, 100, 0, text);
}

void TestPartialCheckpointKeepsTailAndAllowsLateSuffixEvent() {
  Mesh mesh;
  mesh.Admit();
  mesh.Settle(true);
  mesh.Equal(3);
  Publish(mesh, {"H", "A", "B"}, 35);
  mesh.Settle(true);
  auto const h60 = mesh.R("H").Append(mesh.room_id, "|H60", 60);
  auto const a50 = mesh.R("A").Append(mesh.room_id, "|A50", 50);
  auto const b70 = mesh.R("B").Append(mesh.room_id, "|B70", 70);
  (void)b70;
  CHECK(mesh.network.PendingPackets().empty());
  Compact(mesh, {"H"}, 35, 1, "room:|H10|A20|H60");
  auto const& state = *mesh.R("H").Room(mesh.room_id).local_state;
  CHECK(state.local_frames.size() == 1 &&
        state.local_frames.front().event_id == h60);
  CHECK(!state.FindLocalFrame(mesh.h10) && !state.FindLocalFrame(mesh.join_id));
  mesh.Restart("H");
  mesh.Service();
  mesh.Deliver(mesh.Packet(SharedSyncKind::Event, "A", "H", a50));
  CHECK(mesh.R("H").Room(mesh.room_id).text == "room:|H10|A20|A50|H60");
  mesh.Settle(true);
  for (auto const& uid : {"H", "A", "B"}) {
    CHECK(mesh.R(uid).Room(mesh.room_id).text == "room:|H10|A20|A50|H60|B70");
  }
  CHECK(mesh.R("H").Room(mesh.room_id).journal.size() == 3);
  CHECK(mesh.R("A").Room(mesh.room_id).journal.size() == 6);
  CheckColdCheckpoint(mesh, "H", 35, 3, "room:|H10|A20|A50|H60|B70");
  mesh.NoOldResend();
}

void TestAllCompactedRestartAndRetiredDuplicate() {
  Mesh mesh;
  mesh.Admit();
  mesh.Settle(true);
  mesh.Equal(3);
  auto const original =
      std::find_if(mesh.sent.begin(), mesh.sent.end(), [&](auto const& frame) {
        return frame.source == "A" && frame.destination == "H" &&
               frame.message.kind == SharedSyncKind::Event &&
               frame.message.event_id == mesh.a20;
      });
  CHECK(original != mesh.sent.end());
  auto const retired = *original;
  Publish(mesh, {"H", "A", "B"}, 100);
  mesh.Settle(true);
  auto const text = mesh.R("H").Room(mesh.room_id).text;
  Compact(mesh, {"H", "A", "B"}, 100, 0, text);
  mesh.Restart("H");
  mesh.Restart("A");
  mesh.Restart("B");
  bool const replayed = mesh.R("A").endpoint->Send("H", retired.bytes);
  CHECK(replayed);
  mesh.Deliver(mesh.network.PendingPackets().back());
  auto const ack = mesh.Packet(SharedSyncKind::Ack, "H", "A", mesh.a20);
  CHECK(mesh.Decode(ack).received_prefix == (SharedStreamCursor{"A", 1, 20}));
  mesh.Deliver(ack);
  CHECK(mesh.R("H").Room(mesh.room_id).journal.empty());
  CHECK(mesh.R("H").Room(mesh.room_id).text == text);

  // An unknown late Event cannot be smuggled into the folded interval.
  auto late = mesh.R("A").NewText(mesh.room_id, "|late-invalid");
  SharedSyncMessage invalid;
  invalid.node_id = mesh.room_id;
  invalid.packet_id = invalid.event_id = late.id();
  invalid.timestamp_us = 90;
  invalid.stamp = {"A", 2, late.id(), 90};
  bool const captured = CaptureSharedGraph(*late, invalid.graph);
  CHECK(captured);
  std::vector<std::uint8_t> bytes;
  bool const encoded = SerializeSharedSyncMessage(invalid, bytes);
  CHECK(encoded);
  auto const saves = mesh.R("H").durable.saves;
  auto const sent_count = mesh.sent.size();
  bool const injected = mesh.R("A").endpoint->Send("H", bytes);
  CHECK(injected);
  mesh.Deliver(mesh.network.PendingPackets().back());
  CHECK(mesh.R("H").durable.saves == saves && mesh.sent.size() == sent_count);
  CHECK(mesh.R("H").Room(mesh.room_id).journal.empty());
  CHECK(
      !mesh.R("H").Room(mesh.room_id).local_state->FindStreamEvent(late.id()));

  auto const h110 = mesh.R("H").Append(mesh.room_id, "|H110", 110);
  auto const a120 = mesh.R("A").Append(mesh.room_id, "|A120", 120);
  auto const b130 = mesh.R("B").Append(mesh.room_id, "|B130", 130);
  CHECK(mesh.R("H")
            .Room(mesh.room_id)
            .local_state->FindStreamEvent(h110)
            ->index == 3);
  CHECK(mesh.R("A")
            .Room(mesh.room_id)
            .local_state->FindStreamEvent(a120)
            ->index == 2);
  CHECK(mesh.R("B")
            .Room(mesh.room_id)
            .local_state->FindStreamEvent(b130)
            ->index == 1);
  mesh.Settle(true);
  for (auto const& uid : {"H", "A", "B"}) {
    CHECK(mesh.R(uid).Room(mesh.room_id).text == text + "|H110|A120|B130");
    CHECK(mesh.R(uid).Room(mesh.room_id).journal.size() == 3);
  }
  CHECK(mesh.Count("A", "B", a120) == 1 && mesh.Count("B", "A", b130) == 1);
  mesh.NoOldResend();
}

void TestAdmissionAfterHostCheckpointDoesNotReplayOldHistory() {
  Mesh mesh;
  Publish(mesh, {"H", "A"}, 100);
  mesh.Settle(true);
  Compact(mesh, {"H"}, 100, 0, "room:|H10|A20");
  CHECK(mesh.R("A").Room(mesh.room_id).journal.size() == 2);
  CHECK(mesh.R("A").Room(mesh.room_id).local_state->FindLocalFrame(mesh.a20));
  mesh.Admit(110);
  mesh.Service();
  auto const snapshot = mesh.Packet(SharedSyncKind::InitialState, "H", "B");
  auto const initial = mesh.Decode(snapshot);
  CHECK(initial.checkpoint_us == 100 && initial.covered_events.size() == 3);
  CHECK(initial.graph.layers.count(mesh.h10) == 0);
  CHECK(initial.graph.layers.count(mesh.a20) == 0);
  CHECK(std::find(initial.covered_events.begin(), initial.covered_events.end(),
                  mesh.a20) != initial.covered_events.end());
  mesh.Deliver(mesh.Packet(SharedSyncKind::Event, "H", "A", mesh.join_id));
  mesh.Service();
  mesh.NoOldResend();
  mesh.Deliver(snapshot);
  mesh.Settle(true);
  for (auto const& uid : {"H", "A", "B"}) {
    CHECK(mesh.R(uid).Room(mesh.room_id).text == "room:|H10|A20");
    CHECK(mesh.R(uid).Room(mesh.room_id).participants.size() == 3);
  }
  CHECK(mesh.R("B").Room(mesh.room_id).local_state->checkpoint_us == 100);
  CHECK(mesh.R("B").Room(mesh.room_id).local_state->timestamp_floor >= 110);
  CHECK(mesh.R("B").Room(mesh.room_id).journal.size() == 1);
  auto early = mesh.R("B").NewText(mesh.room_id, "|before-snapshot");
  bool const bad_time =
      mesh.R("B").runtime->CommitLocal(mesh.room_id, early, 105);
  CHECK(!bad_time);
  bool const stale_roster = mesh.R("H").runtime->CompactConfirmed(mesh.room_id);
  CHECK(!stale_roster);  // Old H/A reports do not authorize the new roster.
  Publish(mesh, {"H", "A", "B"}, 150);
  mesh.Settle(true);
  Compact(mesh, {"H", "A", "B"}, 150, 0, "room:|H10|A20");
  mesh.NoOldResend();
}

void TestWrongRosterAndMarkerAuthorAreRejected() {
  Mesh mesh;
  mesh.Admit();
  mesh.Settle(true);
  mesh.Equal(3);
  Publish(mesh, {"H"}, 100);
  mesh.Service();
  auto const packet = mesh.Packet(SharedSyncKind::Progress, "H", "A");
  auto const original = mesh.Decode(packet);
  auto reject = [&](SharedSyncMessage const& bad) {
    auto const saves = mesh.R("A").durable.saves;
    auto const sent_count = mesh.sent.size();
    auto const rejects = mesh.R("A").runtime->rejected_messages();
    std::vector<std::uint8_t> bytes;
    bool const encoded = SerializeSharedSyncMessage(bad, bytes);
    CHECK(encoded);
    bool const injected = mesh.R("H").endpoint->Send("A", bytes);
    CHECK(injected);
    mesh.Deliver(mesh.network.PendingPackets().back());
    CHECK(mesh.R("A").runtime->rejected_messages() > rejects);
    CHECK(mesh.R("A").durable.saves == saves && mesh.sent.size() == sent_count);
    CHECK(!mesh.R("A").Room(mesh.room_id).local_state->FindProgress("H"));
  };
  auto bad = original;
  bad.progress.members.erase(std::remove(bad.progress.members.begin(),
                                         bad.progress.members.end(), "B"),
                             bad.progress.members.end());
  bad.progress.received.erase(
      std::remove_if(
          bad.progress.received.begin(), bad.progress.received.end(),
          [](auto const& cursor) { return cursor.author_uid == "B"; }),
      bad.progress.received.end());
  reject(bad);
  bad = original;
  bad.progress.author_uid = "A";
  reject(bad);
  bad = original;
  auto cursor =
      std::find_if(bad.progress.received.begin(), bad.progress.received.end(),
                   [](auto const& value) { return value.author_uid == "A"; });
  CHECK(cursor != bad.progress.received.end() && cursor->index == 1);
  ++cursor->timestamp_us;  // A known index must bind the saved timestamp.
  reject(bad);
  mesh.Settle(true);
}

void TestCoverageReceiptAfterJoinWasCompactedAndRestarted() {
  Mesh mesh;
  mesh.Admit();
  mesh.Service();
  mesh.Deliver(mesh.Packet(SharedSyncKind::Event, "H", "A", mesh.join_id));
  mesh.Deliver(mesh.Packet(SharedSyncKind::InitialState, "H", "B"));
  mesh.Deliver(mesh.Packet(SharedSyncKind::Ack, "B", "H", {},
                           SharedSyncKind::InitialState));
  mesh.Service();
  auto const notice = mesh.Packet(SharedSyncKind::CoverageReceipt, "H", "A");
  auto const notice_id = mesh.Decode(notice).packet_id;
  mesh.Drop(notice);
  CHECK(mesh.R("H").Peer(mesh.room_id, "A").FindPending(notice_id));
  CHECK(!mesh.R("A").Peer(mesh.room_id, "B").coverage_confirmed);

  // Reports prove durable Event receipt without requiring receipt-ACK
  // bookkeeping to finish first. Keep logical time below the notice retry.
  Publish(mesh, {"H", "A", "B"}, 100);
  mesh.Service();
  mesh.Drain(true);
  CHECK(mesh.R("H").Peer(mesh.room_id, "A").FindPending(notice_id));
  CHECK(!mesh.R("A").Peer(mesh.room_id, "B").coverage_confirmed);
  Compact(mesh, {"A"}, 100, 0, "room:|H10|A20");
  CHECK(mesh.R("A")
            .Room(mesh.room_id)
            .local_state->FindStreamEvent(mesh.join_id));
  mesh.Restart("A");
  CHECK(mesh.R("A").Room(mesh.room_id).journal.empty());
  CHECK(!mesh.R("A").Peer(mesh.room_id, "B").coverage_confirmed);
  mesh.Service(kRetry);
  auto const retry = mesh.Packet(SharedSyncKind::CoverageReceipt, "H", "A");
  CHECK(retry.bytes == notice.bytes);
  mesh.Deliver(retry);
  auto const ack = mesh.Packet(SharedSyncKind::Ack, "A", "H", {},
                               SharedSyncKind::CoverageReceipt);
  CHECK(mesh.R("A").Peer(mesh.room_id, "B").coverage_confirmed);
  CheckColdCheckpoint(mesh, "A", 100, 0, "room:|H10|A20");
  mesh.Deliver(ack);
  CHECK(!mesh.R("H").Peer(mesh.room_id, "A").FindPending(notice_id));
  CHECK(mesh.R("H").Peer(mesh.room_id, "A").HasNoticeDelivered(notice_id));
  mesh.Settle(true);
  CHECK(mesh.R("A").Room(mesh.room_id).journal.empty());
  CHECK(mesh.R("A").Room(mesh.room_id).text ==
        mesh.R("H").Room(mesh.room_id).text);
  mesh.NoOldResend();
}

}  // namespace
}  // namespace apptraverse::test

int main(int argc, char** argv) {
  apptraverse::EnsureSharedNodeRegistration();
  unsigned executed = 0;
  auto run = [&](char const* name, void (*test)()) {
    if (argc == 1 || std::string{argv[1]} == name) {
      std::cout << name << std::endl;
      test();
      ++executed;
    }
  };
  run("TestOverlappingInitialAdmissionsDoNotNotifySnapshotCoveredPeer",
      apptraverse::test::TestOverlappingInitialAdmissionsDoNotNotifySnapshotCoveredPeer);
  run("TestJoinFirstImmediateDirectDelivery",
      apptraverse::test::TestJoinFirstImmediateDirectDelivery);
  run("TestSnapshotAndReceiptBeforeJoin",
      apptraverse::test::TestSnapshotAndReceiptBeforeJoin);
  run("TestMissingOwnHistoryUsesOriginalFrameAfterHostAck",
      apptraverse::test::TestMissingOwnHistoryUsesOriginalFrameAfterHostAck);
  run("TestSnapshotReceiptDoesNotAcknowledgeAnotherRecipient",
      apptraverse::test::TestSnapshotReceiptDoesNotAcknowledgeAnotherRecipient);
  run("TestLostControlPacketsAndRestartEveryPhase",
      apptraverse::test::TestLostControlPacketsAndRestartEveryPhase);
  run("TestOfflineAndAllThreeRestartWithDirectPending",
      apptraverse::test::TestOfflineAndAllThreeRestartWithDirectPending);
  run("TestHostOfflineLeavesDirectClientsWorking",
      apptraverse::test::TestHostOfflineLeavesDirectClientsWorking);
  run("TestLegacyNativeEventAfterAdmission",
      apptraverse::test::TestLegacyNativeEventAfterAdmission);
  run("TestContinuousAckGapLossAndSparseRestart",
      apptraverse::test::TestContinuousAckGapLossAndSparseRestart);
  run("TestSnapshotPreservesSparseStreamCoverage",
      apptraverse::test::TestSnapshotPreservesSparseStreamCoverage);
  run("TestInvalidPrefixCannotCancelPending",
      apptraverse::test::TestInvalidPrefixCannotCancelPending);
  run("TestMonotonicAuthorClockAndSnapshotFloorSurviveRestart",
      apptraverse::test::
          TestMonotonicAuthorClockAndSnapshotFloorSurviveRestart);
  run("TestInboundStreamIdentityAndTimestampConflicts",
      apptraverse::test::TestInboundStreamIdentityAndTimestampConflicts);
  run("TestMalformedSnapshotStreamMetadataDoesNotAttach",
      apptraverse::test::TestMalformedSnapshotStreamMetadataDoesNotAttach);
  run("TestSilentParticipantBlocksCheckpointAndEmptyIntervalAdvances",
      apptraverse::test::
          TestSilentParticipantBlocksCheckpointAndEmptyIntervalAdvances);
  run("TestProgressBeforeEventAndStaleReceiptRequireRefresh",
      apptraverse::test::TestProgressBeforeEventAndStaleReceiptRequireRefresh);
  run("TestProgressLossOfflineAndRestartPreservePromise",
      apptraverse::test::TestProgressLossOfflineAndRestartPreservePromise);
  run("TestPartialCheckpointKeepsTailAndAllowsLateSuffixEvent",
      apptraverse::test::
          TestPartialCheckpointKeepsTailAndAllowsLateSuffixEvent);
  run("TestAllCompactedRestartAndRetiredDuplicate",
      apptraverse::test::TestAllCompactedRestartAndRetiredDuplicate);
  run("TestAdmissionAfterHostCheckpointDoesNotReplayOldHistory",
      apptraverse::test::
          TestAdmissionAfterHostCheckpointDoesNotReplayOldHistory);
  run("TestWrongRosterAndMarkerAuthorAreRejected",
      apptraverse::test::TestWrongRosterAndMarkerAuthorAreRejected);
  run("TestCoverageReceiptAfterJoinWasCompactedAndRestarted",
      apptraverse::test::TestCoverageReceiptAfterJoinWasCompactedAndRestarted);
  if (!executed) return 2;
  std::cout << "shared_three_peer_test OK\n";
}
