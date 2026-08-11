#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "aether/clock.h"
#include "aether/domain_storage/ram_domain_storage.h"

#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_graph_copy.h"
#include "apptraverse/object_link.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/sync_packet.h"
#include "model/chat.h"
#include "model/chat_events.h"
#include "model/chat_presenter.h"
#include "model/client.h"
#include "model/registration.h"

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
class PacketLocalNode;
class PacketRootNode;
class PacketProbeEvent;

class PacketSharedNode : public NodeFor<PacketSharedNode> {
  APPTRAVERSE_OBJECT(PacketSharedNode, Node, 0)

 protected:
  PacketSharedNode() = default;

 public:
  explicit PacketSharedNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(label), AE_MMBR(next))

  std::string label;
  SharedPtr<PacketSharedNode> next;
};

class PacketLocalNode : public NodeFor<PacketLocalNode> {
  APPTRAVERSE_OBJECT(PacketLocalNode, Node, 0)

 protected:
  PacketLocalNode() = default;

 public:
  explicit PacketLocalNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(label))

  std::string label;
};

class PacketRootNode : public NodeFor<PacketRootNode> {
  APPTRAVERSE_OBJECT(PacketRootNode, Node, 0)

 protected:
  PacketRootNode() = default;

 public:
  explicit PacketRootNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(label), AE_MMBR(shared_peer), AE_MMBR(local_peer))

  std::string label;
  SharedPtr<PacketSharedNode> shared_peer;
  LocalPtr<PacketLocalNode> local_peer;

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
APPTRAVERSE_REGISTER(PacketLocalNode);
APPTRAVERSE_REGISTER(PacketRootNode);
APPTRAVERSE_REGISTER(PacketProbeEvent);

struct RecordingHandler : SyncPacketHandler {
  int node_state = 0;
  int event = 0;
  int ack = 0;

  void Handle(NodeStatePacket const&) override { ++node_state; }
  void Handle(EventPacket const&) override { ++event; }
  void Handle(AckPacket const&) override { ++ack; }
};

void TestPacketClassVersionsAndDispatch() {
  EnsureObjectRegistration();
  CHECK(SyncPacket::kVersion == 0);
  CHECK(NodeStatePacket::kVersion == 0);
  CHECK(EventPacket::kVersion == 0);
  CHECK(AckPacket::kVersion == 0);

  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};
  auto packet = NodeStatePacket::ptr::Create(ae::CreateWith{domain});
  packet.Save();

  auto decoded = SyncPacketCodec{}.Decode(SyncPacketCodec{}.Encode(packet));
  CHECK(decoded.packet.is_loaded());
  CHECK(decoded.packet.id() == packet.id());
  CHECK(decoded.packet->GetClassId() == NodeStatePacket::kClassId);

  RecordingHandler handler;
  decoded.packet->Dispatch(handler);
  CHECK(handler.node_state == 1);
  CHECK(handler.event == 0);
  CHECK(handler.ack == 0);
}

void TestAckPacketRoundtrip() {
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};
  auto packet = AckPacket::ptr::Create(ae::CreateWith{domain});
  auto const packet_id = packet.id();
  packet->acknowledged_packet_id = ae::ObjId{99};
  packet.Save();

  auto decoded = SyncPacketCodec{}.Decode(SyncPacketCodec{}.Encode(packet));
  CHECK(decoded.packet.id() == packet_id);
  auto concrete = AckPacket::ptr{decoded.packet};
  concrete.Load();
  CHECK(concrete->acknowledged_packet_id.id() == 99);

  RecordingHandler handler;
  concrete->Dispatch(handler);
  CHECK(handler.ack == 1);
}

void TestEncodeDeterministic() {
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};
  auto packet = AckPacket::ptr::Create(ae::CreateWith{domain});
  packet->acknowledged_packet_id = ae::ObjId{123};
  packet.Save();
  SyncPacketCodec codec;
  auto const a = codec.Encode(packet);
  auto const b = codec.Encode(packet);
  CHECK(a == b);
}

void TestDecodeOwnsFreshDomain() {
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};
  auto packet = AckPacket::ptr::Create(ae::CreateWith{domain});
  packet->acknowledged_packet_id = ae::ObjId{1};
  packet.Save();
  auto const bytes = SyncPacketCodec{}.Encode(packet);

  auto decoded_a = SyncPacketCodec{}.Decode(bytes);
  auto decoded_b = SyncPacketCodec{}.Decode(bytes);
  CHECK(decoded_a.domain.get() != decoded_b.domain.get());
  CHECK(decoded_a.packet.domain() == decoded_a.domain.get());
  CHECK(decoded_b.packet.domain() == decoded_b.domain.get());
  CHECK(decoded_a.packet.domain() != &domain);
}

void TestLocalPtrFilteredFromEncodedGraph() {
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};

  auto local =
      PacketLocalNode::ptr::Create(ae::CreateWith{domain}.with_id(40));
  local->label = "secret-local";
  local.Save();

  auto shared =
      PacketSharedNode::ptr::Create(ae::CreateWith{domain}.with_id(41));
  shared->label = "shared";
  shared.Save();

  auto root = PacketRootNode::ptr::Create(ae::CreateWith{domain}.with_id(42));
  auto base = PacketRootNode::ptr::Create(ae::CreateWith{domain}.with_id(43));
  root->base = base;
  root->CaptureBaseState();
  root->label = "root";
  root->shared_peer = shared;
  root->local_peer = local;
  root.Save();

  ae::RamDomainStorage build_storage;
  ae::Domain build_domain{ae::Now(), build_storage};
  CopyObjectGraph(root, storage, build_domain, build_storage,
                  SharedCopyMode::kCopyLoadedTargets);
  auto build_root =
      PacketRootNode::ptr::Declare(ae::CreateWith{build_domain}.with_id(42));
  build_root.Load();

  auto packet = NodeStatePacket::ptr::Create(ae::CreateWith{build_domain});
  packet->node = build_root;
  auto const bytes = SyncPacketCodec{}.Encode(packet);

  auto decoded = SyncPacketCodec{}.Decode(bytes);
  auto node_packet = NodeStatePacket::ptr{decoded.packet};
  node_packet.Load();
  CHECK(node_packet->node.is_loaded());
  auto decoded_root = PacketRootNode::ptr{node_packet->node};
  decoded_root.Load();
  CHECK(decoded_root->label == "root");
  CHECK(decoded_root->shared_peer.is_valid());
  decoded_root->shared_peer.Load();
  CHECK(decoded_root->shared_peer->label == "shared");
  CHECK(!decoded_root->local_peer.is_valid());
  CHECK(!StorageHasObject(*decoded.storage, local.id()));
}

void TestSharedPtrCycleRoundtrip() {
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};

  auto a = PacketSharedNode::ptr::Create(ae::CreateWith{domain}.with_id(50));
  auto b = PacketSharedNode::ptr::Create(ae::CreateWith{domain}.with_id(51));
  auto a_base =
      PacketSharedNode::ptr::Create(ae::CreateWith{domain}.with_id(52));
  auto b_base =
      PacketSharedNode::ptr::Create(ae::CreateWith{domain}.with_id(53));
  a->label = "a";
  b->label = "b";
  a->base = a_base;
  b->base = b_base;
  a->CaptureBaseState();
  b->CaptureBaseState();
  a->next = b;
  b->next = a;
  a.Save();
  b.Save();

  ae::RamDomainStorage build_storage;
  ae::Domain build_domain{ae::Now(), build_storage};
  CopyObjectGraph(a, storage, build_domain, build_storage,
                  SharedCopyMode::kCopyLoadedTargets);
  auto build_a =
      PacketSharedNode::ptr::Declare(ae::CreateWith{build_domain}.with_id(50));
  build_a.Load();

  auto packet = NodeStatePacket::ptr::Create(ae::CreateWith{build_domain});
  packet->node = build_a;
  auto decoded =
      SyncPacketCodec{}.Decode(SyncPacketCodec{}.Encode(packet));
  auto node_packet = NodeStatePacket::ptr{decoded.packet};
  node_packet.Load();
  auto decoded_a = PacketSharedNode::ptr{node_packet->node};
  decoded_a.Load();
  CHECK(decoded_a->label == "a");
  decoded_a->next.Load();
  CHECK(decoded_a->next->label == "b");
  decoded_a->next->next.Load();
  CHECK(decoded_a->next->next.id() == decoded_a.id());
}

void TestChatNodeStateAndEventRoundtrips() {
  EnsureSingleClientChatRegistration();

  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};

  auto client_base =
      Client::ptr::Create(ae::CreateWith{domain}.with_id(201));
  auto client = Client::ptr::Create(ae::CreateWith{domain}.with_id(202));
  client->name = "Alice";
  client->base = client_base;
  client->CaptureBaseState();
  client.Save();

  auto chat_base = Chat::ptr::Create(ae::CreateWith{domain}.with_id(101));
  auto chat = Chat::ptr::Create(ae::CreateWith{domain}.with_id(102));
  chat->base = chat_base;
  chat->CaptureBaseState();

  auto join = JoinClientEvent::ptr::Create(ae::CreateWith{domain}.with_id(301));
  join->client = client;
  chat->Commit(join);

  auto message =
      AddMessageEvent::ptr::Create(ae::CreateWith{domain}.with_id(401));
  message->author = client;
  message->text = "hello";
  chat->Commit(message);
  chat.Save();

  {
    ae::RamDomainStorage build_storage;
    ae::Domain build_domain{ae::Now(), build_storage};
    CopyObjectGraph(chat, storage, build_domain, build_storage,
                    SharedCopyMode::kCopyLoadedTargets);
    auto build_chat =
        Chat::ptr::Declare(ae::CreateWith{build_domain}.with_id(102));
    build_chat.Load();
    auto packet = NodeStatePacket::ptr::Create(ae::CreateWith{build_domain});
    packet->node = build_chat;
    auto decoded =
        SyncPacketCodec{}.Decode(SyncPacketCodec{}.Encode(packet));
    auto node_packet = NodeStatePacket::ptr{decoded.packet};
    node_packet.Load();
    auto decoded_chat = Chat::ptr{node_packet->node};
    decoded_chat.Load();
    CHECK(decoded_chat->journal.size() == 2);
    CHECK(decoded_chat->entries.size() == 2);
  }

  {
    auto source_event = message;
    source_event.Load();
    ae::RamDomainStorage build_storage;
    ae::Domain build_domain{ae::Now(), build_storage};
    CopyObjectGraph(source_event, storage, build_domain, build_storage,
                    SharedCopyMode::kCopyLoadedTargets);
    auto build_event =
        AddMessageEvent::ptr::Declare(ae::CreateWith{build_domain}.with_id(401));
    build_event.Load();
    auto packet = EventPacket::ptr::Create(ae::CreateWith{build_domain});
    packet->target_node_id = chat.id();
    packet->timestamp_us = chat->journal.back().timestamp_us;
    packet->event = build_event;
    auto decoded =
        SyncPacketCodec{}.Decode(SyncPacketCodec{}.Encode(packet));
    auto event_packet = EventPacket::ptr{decoded.packet};
    event_packet.Load();
    CHECK(event_packet->target_node_id == chat.id());
    CHECK(event_packet->event.is_loaded());
    auto decoded_event = AddMessageEvent::ptr{event_packet->event};
    decoded_event.Load();
    CHECK(decoded_event->text == "hello");
    decoded_event->author.Load();
    CHECK(decoded_event->author->name == "Alice");
  }
}

void TestNoSenderIdentityFields() {
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};
  auto packet = NodeStatePacket::ptr::Create(ae::CreateWith{domain});
  auto const identity = packet.id();
  CHECK(identity.IsValid());
  packet.Save();

  auto ack = AckPacket::ptr::Create(ae::CreateWith{domain});
  ack->acknowledged_packet_id = identity;
  ack.Save();

  auto decoded = SyncPacketCodec{}.Decode(SyncPacketCodec{}.Encode(ack));
  auto concrete = AckPacket::ptr{decoded.packet};
  concrete.Load();
  CHECK(concrete->acknowledged_packet_id == identity);
  CHECK(concrete.id() != identity);
}

}  // namespace apptraverse::test

int main() {
  using namespace apptraverse::test;
  apptraverse::EnsureObjectRegistration();
  TestPacketClassVersionsAndDispatch();
  TestAckPacketRoundtrip();
  TestEncodeDeterministic();
  TestDecodeOwnsFreshDomain();
  TestLocalPtrFilteredFromEncodedGraph();
  TestSharedPtrCycleRoundtrip();
  TestChatNodeStateAndEventRoundtrips();
  TestNoSenderIdentityFields();
  std::cout << "sync_packet_test OK\n";
  return 0;
}
