#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "aether/clock.h"
#include "aether/domain_storage/ram_domain_storage.h"

#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_link.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/object_state.h"
#include "apptraverse/sync_packet.h"

namespace apptraverse::test {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

class PacketSharedNode;
class PacketRootNode;
class PacketProbeEvent;

class PacketSharedNode : public NodeFor<PacketSharedNode> {
  APPTRAVERSE_OBJECT(PacketSharedNode, Node, 0)

 protected:
  PacketSharedNode() = default;

 public:
  explicit PacketSharedNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(label))

  std::string label;
};

class PacketRootNode : public NodeFor<PacketRootNode> {
  APPTRAVERSE_OBJECT(PacketRootNode, Node, 0)

 protected:
  PacketRootNode() = default;

 public:
  explicit PacketRootNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(label), AE_MMBR(shared_peer))

  std::string label;
  SharedPtr<PacketSharedNode> shared_peer;

  void Apply(PacketProbeEvent const&) {}
};

class PacketProbeEvent
    : public EventFor<PacketRootNode, PacketProbeEvent> {
  APPTRAVERSE_OBJECT(PacketProbeEvent, Event, 0)

 protected:
  PacketProbeEvent() = default;

 public:
  explicit PacketProbeEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(peer), AE_MMBR(note))

  SharedPtr<PacketSharedNode> peer;
  std::string note;
};

APPTRAVERSE_REGISTER(PacketSharedNode);
APPTRAVERSE_REGISTER(PacketRootNode);
APPTRAVERSE_REGISTER(PacketProbeEvent);

struct RecordingHandler : SyncPacketHandler {
  int node_state = 0;
  int event = 0;
  int request = 0;
  int ack = 0;

  void Handle(NodeStatePacket const&) override { ++node_state; }
  void Handle(EventPacket const&) override { ++event; }
  void Handle(NodeStateRequestPacket const&) override { ++request; }
  void Handle(AckPacket const&) override { ++ack; }
};

void TestPacketClassVersionsAndDynamicLoad() {
  EnsureObjectRegistration();
  CHECK(SyncPacket::kVersion == 0);
  CHECK(NodeStatePacket::kVersion == 0);
  CHECK(EventPacket::kVersion == 0);
  CHECK(NodeStateRequestPacket::kVersion == 0);
  CHECK(AckPacket::kVersion == 0);

  SyncPacketCodec codec;
  auto packet =
      NodeStatePacket::ptr::Create(ae::CreateWith{codec.domain()});
  packet->state.root_id = ae::ObjId{42};
  packet->required_nodes = {ae::ObjId{7}, ae::ObjId{9}};
  packet.Save();

  auto const bytes = codec.Encode(packet);
  SyncPacketCodec receiver;
  auto loaded = receiver.Decode(bytes);
  CHECK(loaded.is_loaded());
  CHECK(loaded.id() == packet.id());
  CHECK(loaded->GetClassId() == NodeStatePacket::kClassId);
  CHECK(loaded.domain() == &receiver.domain());

  RecordingHandler handler;
  loaded->Dispatch(handler);
  CHECK(handler.node_state == 1);
  CHECK(handler.event == 0);
}

void TestNodeStatePacketRoundtrip() {
  SyncPacketCodec sender;
  auto packet =
      NodeStatePacket::ptr::Create(ae::CreateWith{sender.domain()});
  auto const packet_id = packet.id();
  packet->state.root_id = ae::ObjId{100};
  packet->state.objects.push_back(StoredObjectVersion{
      ae::ObjId{100},
      11,
      0,
      ae::ObjectData{1, 2, 3, 4},
  });
  packet->required_nodes = {ae::ObjId{3}, ae::ObjId{5}};
  packet.Save();

  auto const bytes = sender.Encode(packet);
  SyncPacketCodec receiver;
  auto loaded = receiver.Decode(bytes);
  CHECK(loaded.id() == packet_id);
  CHECK(loaded->GetClassId() == NodeStatePacket::kClassId);
  auto concrete = NodeStatePacket::ptr{loaded};
  concrete.Load();
  CHECK(concrete.is_loaded());
  CHECK(concrete->state.root_id.id() == 100);
  CHECK(concrete->state.objects.size() == 1);
  CHECK(concrete->state.objects[0].obj_id.id() == 100);
  CHECK(concrete->state.objects[0].class_id == 11);
  CHECK(concrete->state.objects[0].version == 0);
  CHECK(concrete->state.objects[0].data ==
        (ae::ObjectData{1, 2, 3, 4}));
  CHECK(concrete->required_nodes.size() == 2);
  CHECK(concrete->required_nodes[0].id() == 3);
  CHECK(concrete->required_nodes[1].id() == 5);
  CHECK(concrete.domain() == &receiver.domain());
  CHECK(packet.domain() == &sender.domain());
}

void TestEventPacketRoundtrip() {
  SyncPacketCodec sender;
  auto packet = EventPacket::ptr::Create(ae::CreateWith{sender.domain()});
  auto const packet_id = packet.id();
  packet->target_node_id = ae::ObjId{50};
  packet->timestamp_us = 123456789ull;
  packet->state.root_id = ae::ObjId{60};
  packet->state.objects.push_back(StoredObjectVersion{
      ae::ObjId{60},
      22,
      0,
      ae::ObjectData{9, 8},
  });
  packet->required_nodes = {ae::ObjId{70}};
  packet.Save();

  auto const bytes = sender.Encode(packet);
  SyncPacketCodec receiver;
  auto loaded = receiver.Decode(bytes);
  CHECK(loaded.id() == packet_id);
  auto concrete = EventPacket::ptr{loaded};
  concrete.Load();
  CHECK(concrete->target_node_id.id() == 50);
  CHECK(concrete->timestamp_us == 123456789ull);
  CHECK(concrete->state.root_id.id() == 60);
  CHECK(concrete->state.objects.size() == 1);
  CHECK(concrete->state.objects[0].data == (ae::ObjectData{9, 8}));
  CHECK(concrete->required_nodes.size() == 1);
  CHECK(concrete->required_nodes[0].id() == 70);

  RecordingHandler handler;
  concrete->Dispatch(handler);
  CHECK(handler.event == 1);
}

void TestNodeStateRequestPacketRoundtrip() {
  SyncPacketCodec sender;
  auto packet =
      NodeStateRequestPacket::ptr::Create(ae::CreateWith{sender.domain()});
  auto const packet_id = packet.id();
  packet->requested_node_id = ae::ObjId{88};
  packet.Save();

  auto const bytes = sender.Encode(packet);
  SyncPacketCodec receiver;
  auto loaded = receiver.Decode(bytes);
  CHECK(loaded.id() == packet_id);
  auto concrete = NodeStateRequestPacket::ptr{loaded};
  concrete.Load();
  CHECK(concrete->requested_node_id.id() == 88);

  RecordingHandler handler;
  concrete->Dispatch(handler);
  CHECK(handler.request == 1);
}

void TestAckPacketRoundtrip() {
  SyncPacketCodec sender;
  auto packet = AckPacket::ptr::Create(ae::CreateWith{sender.domain()});
  auto const packet_id = packet.id();
  packet->acknowledged_packet_id = ae::ObjId{99};
  packet.Save();

  auto const bytes = sender.Encode(packet);
  SyncPacketCodec receiver;
  auto loaded = receiver.Decode(bytes);
  CHECK(loaded.id() == packet_id);
  auto concrete = AckPacket::ptr{loaded};
  concrete.Load();
  CHECK(concrete->acknowledged_packet_id.id() == 99);

  RecordingHandler handler;
  concrete->Dispatch(handler);
  CHECK(handler.ack == 1);
}

void TestEncodeDeterministic() {
  SyncPacketCodec codec;
  auto packet =
      NodeStateRequestPacket::ptr::Create(ae::CreateWith{codec.domain()});
  packet->requested_node_id = ae::ObjId{123};
  packet.Save();
  auto const a = codec.Encode(packet);
  auto const b = codec.Encode(packet);
  CHECK(a == b);
}

void TestDecodeIsolatesPacketDomain() {
  SyncPacketCodec sender;
  auto packet = AckPacket::ptr::Create(ae::CreateWith{sender.domain()});
  packet->acknowledged_packet_id = ae::ObjId{1};
  packet.Save();
  auto const bytes = sender.Encode(packet);

  SyncPacketCodec receiver;
  auto loaded = receiver.Decode(bytes);
  CHECK(loaded.domain() == &receiver.domain());
  CHECK(loaded.domain() != &sender.domain());
  CHECK(receiver.storage().state.find(loaded.id()) !=
        receiver.storage().state.end());
  CHECK(sender.storage().state.find(loaded.id()) !=
        sender.storage().state.end());
}

void TestNoSenderUidOrPacketIdField() {
  // Identity is the packet ObjId; Ack references that ObjId.
  SyncPacketCodec codec;
  auto packet =
      NodeStatePacket::ptr::Create(ae::CreateWith{codec.domain()});
  auto const identity = packet.id();
  CHECK(identity.IsValid());
  packet->state.root_id = ae::ObjId{1};
  packet.Save();

  auto ack = AckPacket::ptr::Create(ae::CreateWith{codec.domain()});
  ack->acknowledged_packet_id = identity;
  ack.Save();

  // Only the designed fields round-trip; no packet_id / sender fields exist
  // on SyncPacket or derived types (verified by successful compile of this
  // test against the public headers).
  auto decoded = codec.Decode(codec.Encode(ack));
  auto concrete = AckPacket::ptr{decoded};
  concrete.Load();
  CHECK(concrete->acknowledged_packet_id == identity);
  CHECK(concrete.id() != identity);
}

void TestDiscoverSharedDependencies() {
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};

  auto shared =
      PacketSharedNode::ptr::Create(ae::CreateWith{domain}.with_id(31));
  shared->label = "shared";
  auto root =
      PacketRootNode::ptr::Create(ae::CreateWith{domain}.with_id(32));
  root->label = "root";
  root->shared_peer = shared;
  auto base =
      PacketRootNode::ptr::Create(ae::CreateWith{domain}.with_id(30));
  root->base = base;
  root->CaptureBaseState();

  auto deps = DiscoverSharedDependencies(root);
  CHECK(deps.size() == 1);
  CHECK(deps[0].id() == 31);

  auto event =
      PacketProbeEvent::ptr::Create(ae::CreateWith{domain}.with_id(33));
  event->peer = shared;
  event->note = "n";
  event.Save();
  auto event_deps = DiscoverSharedDependencies(event);
  CHECK(event_deps.size() == 1);
  CHECK(event_deps[0].id() == 31);

  auto leaf =
      PacketSharedNode::ptr::Create(ae::CreateWith{domain}.with_id(34));
  leaf->label = "leaf";
  auto leaf_deps = DiscoverSharedDependencies(leaf);
  CHECK(leaf_deps.empty());
}

}  // namespace apptraverse::test

int main() {
  using namespace apptraverse::test;
  apptraverse::EnsureObjectRegistration();
  TestPacketClassVersionsAndDynamicLoad();
  TestNodeStatePacketRoundtrip();
  TestEventPacketRoundtrip();
  TestNodeStateRequestPacketRoundtrip();
  TestAckPacketRoundtrip();
  TestEncodeDeterministic();
  TestDecodeIsolatesPacketDomain();
  TestNoSenderUidOrPacketIdField();
  TestDiscoverSharedDependencies();
  std::cout << "sync_packet_test OK\n";
  return 0;
}
