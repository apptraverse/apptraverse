#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "apptraverse/memory_link.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/shared_graph.h"
#include "apptraverse/shared_node.h"

namespace apptraverse::test {
namespace {

#define CHECK(condition)                                                    \
  do {                                                                      \
    if (!(condition)) {                                                      \
      std::cerr << "CHECK failed: " #condition << " at " << __LINE__ << '\n'; \
      std::exit(1);                                                         \
    }                                                                       \
  } while (false)

class PrivateTransport : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::SharedPrivateTransport",
                           PrivateTransport, ae::Obj, 0)
 protected:
  PrivateTransport() = default;
 public:
  explicit PrivateTransport(ae::ObjProp prop) : Obj{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(secret))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) { dnv(base_, secret); }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const { dnv(base_, secret); }
  std::string secret;
};

class Participant : public NodeFor<Participant> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::NativeSharedParticipant",
                           Participant, Node, 0)
 protected:
  Participant() = default;
 public:
  explicit Participant(ae::ObjProp prop) : NodeFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(name), AE_MMBR(link))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) { dnv(base_, name, link); }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const { dnv(base_, name, link); }
  std::string name;
  SharedLinkDescriptor::ptr link;
};

class AppendRoomTextEvent;
class JoinRoomEvent;
class Room : public NodeFor<Room, SharedNode> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::NativeSharedRoom", Room,
                           SharedNode, 0)
 protected:
  Room() = default;
 public:
  explicit Room(ae::ObjProp prop) : NodeFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(text), AE_MMBR(participants))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) { dnv(base_, text, participants); }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, text, participants);
  }
  void Apply(AppendRoomTextEvent const& event);
  void Apply(JoinRoomEvent const& event);
  void InsertAt(Event::ptr event, std::uint64_t timestamp) {
    InsertEvent(EventRecord{.event = std::move(event),
                            .order = {.timestamp_us = timestamp},
                            .retained_since_us = timestamp});
  }
  std::string text;
  std::vector<Participant::ptr> participants;
};

class JoinRoomEvent : public EventFor<Room, JoinRoomEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::JoinNativeSharedRoom",
                           JoinRoomEvent, Event, 0)
 protected:
  JoinRoomEvent() = default;
 public:
  explicit JoinRoomEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(participant))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) { dnv(base_, participant); }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const { dnv(base_, participant); }
  Participant::ptr participant;
};

class AppendRoomTextEvent : public EventFor<Room, AppendRoomTextEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::AppendNativeRoomText",
                           AppendRoomTextEvent, Event, 0)
 protected:
  AppendRoomTextEvent() = default;
 public:
  explicit AppendRoomTextEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(text))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) { dnv(base_, text); }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const { dnv(base_, text); }
  std::string text;
};

void Room::Apply(AppendRoomTextEvent const& event) { text += event.text; }
void Room::Apply(JoinRoomEvent const& event) {
  participants.push_back(event.participant);
}

APPTRAVERSE_REGISTER(PrivateTransport);
APPTRAVERSE_REGISTER(Participant);
APPTRAVERSE_REGISTER(Room);
APPTRAVERSE_REGISTER(AppendRoomTextEvent);
APPTRAVERSE_REGISTER(JoinRoomEvent);

SharedLinkDescriptor::ptr MakeDescriptor(ae::Domain& domain,
                                          std::string uid,
                                          ae::ObjPtr<ae::Obj> local = {}) {
  auto link = SharedLinkDescriptor::ptr::Create(ae::CreateWith{domain});
  link->endpoint_uid = std::move(uid);
  link->local_transport = local;
  InitializeRuntimeNode(*link);
  return link;
}

SharedPeerState::ptr MakePeer(ae::Domain& domain,
                              SharedLinkDescriptor::ptr link) {
  auto state = SharedPeerState::ptr::Create(ae::CreateWith{domain});
  state->link = std::move(link);
  state->delivered_events.clear();
  InitializeRuntimeNode(*state);
  return state;
}

SharedNodeLocalState::ptr MakeLocalState(ae::Domain& domain) {
  auto state = SharedNodeLocalState::ptr::Create(ae::CreateWith{domain});
  state->peers.clear();
  InitializeRuntimeNode(*state);
  return state;
}

// These tests intentionally exercise native snapshots into fresh Domains.
// They do not implement repeated packet import, ACK/retry or reconciliation.
void TestReplayKeepsIndependentLocalDeliveryAndColdStorage() {
  ae::RamDomainStorage storage;
  ae::ObjId room_id;
  ae::ObjId delivered_id;
  ae::ObjId private_id;
  {
    ae::Domain domain{storage};
    auto host = MakeDescriptor(domain, "H");
    auto private_transport = PrivateTransport::ptr::Create(ae::CreateWith{domain});
    private_transport->secret = "local socket configuration";
    private_id = private_transport.id();
    auto a = MakeDescriptor(domain, "A", private_transport);
    auto b = MakeDescriptor(domain, "B");
    auto state_a = MakePeer(domain, a);
    auto state_b = MakePeer(domain, b);
    auto room = Room::ptr::Create(ae::CreateWith{domain});
    room->text = "base";
    room->participants.clear();
    room->links = {host, a, b};
    auto local_state = MakeLocalState(domain);
    room->local_state = local_state;
    InitializeRuntimeNode(*room);
    local_state->AddPeer(state_a);
    local_state->AddPeer(state_b);
    room_id = room.id();

    auto late = AppendRoomTextEvent::ptr::Create(ae::CreateWith{domain});
    late->text = "|late";
    delivered_id = late.id();
    room->InsertAt(late, 200);
    state_a->MarkDelivered(delivered_id);
    state_a->MarkDelivered(delivered_id);
    CHECK(state_a->delivered_events.size() == 1);
    CHECK(state_a->journal.size() == 1);
    CHECK(!state_b->HasDelivered(delivered_id));

    auto early = AppendRoomTextEvent::ptr::Create(ae::CreateWith{domain});
    early->text = "|early";
    room->InsertAt(early, 100);  // Forces historical replay.
    CHECK(room->text == "base|early|late");
    CHECK(room->local_state->peers.size() == 2);
    CHECK(&*room->local_state->peers[0] == &*state_a);
    CHECK(room->local_state->peers[0]->HasDelivered(delivered_id));
    CHECK(!room->local_state->peers[1]->HasDelivered(delivered_id));
    CHECK(room->IsJournalCompactionBlocked());
    room->SetJournalRetentionPolicy({.max_events = 0});
    room->CompactJournal(1000);
    CHECK(room->journal.size() == 2);

    // A second SharedNode can use the same Link with distinct progress.
    auto other = Room::ptr::Create(ae::CreateWith{domain});
    other->text = "other";
    other->participants.clear();
    other->links = {a};
    auto other_state = MakePeer(domain, a);
    auto other_local = MakeLocalState(domain);
    other->local_state = other_local;
    InitializeRuntimeNode(*other);
    other_local->AddPeer(other_state);
    CHECK(!other_state->HasDelivered(delivered_id));
    CHECK(&*other->links[0] == &*room->links[1]);
    room.Save();
  }
  {
    ae::Domain domain{storage};
    ae::DomainGraph graph{&domain};
    auto root = graph.LoadRoot(room_id);
    CHECK(root);
    auto room = root.as<Room>();
    CHECK(room);
    CHECK(room->text == "base|early|late");
    CHECK(room->journal.size() == 2);
    CHECK(room->links.size() == 3);
    CHECK(room->local_state->peers.size() == 2);
    CHECK(room->local_state->peers[0]->HasDelivered(delivered_id));
    CHECK(!room->local_state->peers[1]->HasDelivered(delivered_id));
    CHECK(room->local_state->peers[0]->link.id() == room->links[1].id());
    CHECK(&*room->local_state->peers[0]->link == &*room->links[1]);
    auto local = room->links[1]->local_transport.Load().as<PrivateTransport>();
    CHECK(local && local->obj_id == private_id);
    CHECK(local->secret == "local socket configuration");
  }
}

void TestFrozenNativeSnapshotAndJoinGraphThroughMemory() {
  MemoryLinkNetwork network;
  MemoryLink h{network, "H"};
  MemoryLink a{network, "A"};
  MemoryLink b{network, "B"};
  std::vector<std::uint8_t> snapshot_bytes;
  std::vector<std::uint8_t> join_bytes;
  ae::ObjId room_id, join_id, private_id, peer_state_id;
  {
    ae::RamDomainStorage storage;
    ae::Domain domain{storage};
    auto host = MakeDescriptor(domain, "H");
    auto room = Room::ptr::Create(ae::CreateWith{domain});
    room->text = "room at freeze";
    room->participants.clear();
    room->links = {host};
    auto local_state = MakeLocalState(domain);
    room->local_state = local_state;
    InitializeRuntimeNode(*room);
    auto private_transport = PrivateTransport::ptr::Create(ae::CreateWith{domain});
    private_transport->secret = "must never cross the wire";
    private_id = private_transport.id();
    auto descriptor_b = MakeDescriptor(domain, "B", private_transport);
    room->AddLink(descriptor_b);
    room->AddLink(descriptor_b);
    CHECK(room->links.size() == 2);
    CHECK(room->journal.size() == 1);
    auto participant_b = Participant::ptr::Create(ae::CreateWith{domain});
    participant_b->name = "Bob";
    participant_b->link = descriptor_b;
    InitializeRuntimeNode(*participant_b);
    auto join_event = JoinRoomEvent::ptr::Create(ae::CreateWith{domain});
    join_event->participant = participant_b;
    room->Commit(join_event);
    CHECK(room->journal.size() == 2);
    CHECK(room->participants.size() == 1);
    CHECK(&*room->participants[0] == &*join_event->participant);
    CHECK(&*room->participants[0]->link == &*room->links[1]);
    auto state = MakePeer(domain, descriptor_b);
    peer_state_id = state.id();
    local_state->AddPeer(state);
    state->MarkDelivered(room->journal.front().event.id());
    room_id = room.id();
    join_id = room->journal.back().event.id();
    bool frozen = SerializeSharedGraph(*room, snapshot_bytes);
    CHECK(frozen);
    frozen = SerializeSharedGraph(*room->journal.back().event, join_bytes);
    CHECK(frozen);
    bool sent = h.Send("B", snapshot_bytes);
    CHECK(sent);
    sent = h.Send("A", join_bytes);
    CHECK(sent);
    // Later source changes cannot alter the bytes queued in transport.
    auto later = AppendRoomTextEvent::ptr::Create(ae::CreateWith{domain});
    later->text = "|after freeze";
    room->Commit(later);
    CHECK(room->text != "room at freeze");
  }  // Both source Domain and storage are gone before delivery.

  SharedGraphSnapshot received_room;
  SharedGraphSnapshot received_join;
  b.SetReceiver([&](auto const& source, auto const& bytes) {
    CHECK(source == "H");
    bool parsed = DeserializeSharedGraph(bytes, received_room);
    CHECK(parsed);
  });
  a.SetReceiver([&](auto const& source, auto const& bytes) {
    CHECK(source == "H");
    bool parsed = DeserializeSharedGraph(bytes, received_join);
    CHECK(parsed);
  });
  auto packets = network.PendingPackets();
  CHECK(packets.size() == 2);
  bool delayed = network.Delay(packets[0].id, 100);
  CHECK(delayed);
  bool delivered = network.Deliver(packets[1].id);  // Join before snapshot.
  CHECK(delivered);
  CHECK(received_join.root_id == join_id);
  delivered = network.Deliver(packets[0].id);
  CHECK(!delivered);
  network.AdvanceTo(100);
  delivered = network.Deliver(packets[0].id);
  CHECK(delivered);
  CHECK(received_room.root_id == room_id);
  CHECK(received_room.layers.count(private_id) == 0);
  CHECK(received_room.layers.count(peer_state_id) == 0);
  CHECK(received_join.layers.count(private_id) == 0);

  ae::RamDomainStorage room_storage;
  room_storage.state = std::move(received_room.layers);
  ae::Domain room_domain{room_storage};
  ae::DomainGraph graph{&room_domain, ae::GraphSerializationScope::NetworkShared};
  auto root = graph.LoadRoot(room_id);
  CHECK(root);
  auto room = root.as<Room>();
  CHECK(room);
  CHECK(room->text == "room at freeze");
  CHECK(room->links.size() == 2);
  CHECK(room->journal.size() == 2);
  CHECK(!room->local_state.is_valid());
  CHECK(!room->links[1]->local_transport.is_valid());
  auto const added_link =
      room_domain.Find(room->journal[0].event.id()).as<AddSharedLinkEvent>();
  CHECK(added_link);
  CHECK(&*added_link->link == &*room->links[1]);
  auto const joined =
      room_domain.Find(room->journal[1].event.id()).as<JoinRoomEvent>();
  CHECK(joined);
  CHECK(room->participants.size() == 1);
  CHECK(room->participants[0]->name == "Bob");
  CHECK(&*joined->participant == &*room->participants[0]);
  CHECK(&*room->participants[0]->link == &*room->links[1]);

  // A local collection is behind ONE LocalPtr. Incoming graph shape must not
  // resize or replace it, even when this replica has different local progress.
  auto local_state = MakeLocalState(room_domain);
  local_state->AddPeer(MakePeer(room_domain, room->links[0]));
  local_state->AddPeer(MakePeer(room_domain, room->links[1]));
  room->local_state = local_state;
  local_state->peers[0]->MarkDelivered(join_id);
  auto const local_id = local_state.id();
  auto* const local_address = &*local_state;
  ae::DomainGraph refresh{&room_domain,
                           ae::GraphSerializationScope::NetworkShared};
  auto const hydrated = refresh.Load(*room, room_id);
  CHECK(hydrated.has_value() && hydrated->IsOk());
  CHECK(room->local_state.id() == local_id);
  CHECK(&*room->local_state == local_address);
  CHECK(room->local_state->peers.size() == 2);
  CHECK(room->local_state->peers[0]->HasDelivered(join_id));
  CHECK(!room->local_state->peers[1]->HasDelivered(join_id));

  ae::RamDomainStorage join_storage;
  join_storage.state = std::move(received_join.layers);
  ae::Domain join_domain{join_storage};
  ae::DomainGraph join_graph{&join_domain, ae::GraphSerializationScope::NetworkShared};
  auto event = join_graph.LoadRoot(join_id);
  CHECK(event);
  auto join = event.as<JoinRoomEvent>();
  CHECK(join && join->participant->name == "Bob");
  CHECK(join->participant->link->endpoint_uid == "B");
  CHECK(!join->participant->link->local_transport.is_valid());

  // Parsing errors do not publish partial replacement state.
  SharedGraphSnapshot unchanged{room_id, {}};
  auto truncated = snapshot_bytes;
  truncated.pop_back();
  bool parsed = DeserializeSharedGraph(truncated, unchanged);
  CHECK(!parsed && unchanged.root_id == room_id && unchanged.layers.empty());
  auto trailing = snapshot_bytes;
  trailing.push_back(0);
  parsed = DeserializeSharedGraph(trailing, unchanged);
  CHECK(!parsed && unchanged.layers.empty());
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureSharedNodeRegistration();
  apptraverse::test::TestReplayKeepsIndependentLocalDeliveryAndColdStorage();
  apptraverse::test::TestFrozenNativeSnapshotAndJoinGraphThroughMemory();
  std::cout << "shared_node_memory_test OK\n";
}
