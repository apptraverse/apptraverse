// SharedSyncRuntime + MemoryNetwork protocol-level coverage. ChatSession
// end-to-end scenarios live in chat_session_integration_test.cpp.
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj_id.h"

#include "apptraverse/link.h"
#include "apptraverse/memory_transport.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/shared_event_id.h"
#include "apptraverse/shared_network_graph.h"
#include "apptraverse/shared_node.h"
#include "apptraverse/shared_sync_runtime.h"
#include "apptraverse/sync_frame.h"

#include "chat_commands.h"
#include "chat_model.h"
#include "shared_node_demo_model.h"

namespace apptraverse::test {
namespace {

#define CHECK(cond)                                                           \
  do {                                                                        \
    if (!(cond)) {                                                            \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"        \
                << __LINE__ << '\n';                                          \
      std::exit(1);                                                           \
    }                                                                         \
  } while (0)

char const* const kEndpointA = "uid-aaa-111";
char const* const kEndpointB = "uid-bbb-222";
char const* const kEndpointEve = "uid-eve-666";

using apptraverse::example::chat_demo::ChatRoom;
using apptraverse::example::chat_demo::ChatWorkspace;
using apptraverse::example::chat_demo::MessageAddedEvent;
using apptraverse::example::shared_node::SharedValueNode;

ChatWorkspace::ptr CreateWorkspace(ae::Domain& domain, ae::ObjId id) {
  auto ws = ChatWorkspace::ptr::Create(ae::CreateWith{domain}.with_id(id));
  InitializeRuntimeNode(*ws);
  return ws;
}

ChatRoom::ptr CreateRoom(ae::Domain& domain, ae::ObjId id) {
  auto room = ChatRoom::ptr::Create(ae::CreateWith{domain}.with_id(id));
  InitializeRuntimeNode(*room);
  room->SetJournalCompactionBlocked(true);
  return room;
}

MemoryLink::ptr CreateMemoryLink(ae::Domain& domain, ae::ObjId id,
                                 std::string endpoint) {
  auto link = MemoryLink::ptr::Create(ae::CreateWith{domain}.with_id(id));
  link->endpoint_uid = std::move(endpoint);
  InitializeRuntimeNode(*link);
  return link;
}

void SaveWorkspaceGraph(ChatWorkspace::ptr const& ws) {
  ws.Save();
  for (auto& entry : ws->chats) {
    if (entry.is_valid()) {
      entry.Save();
      if (entry->peer_link.is_valid()) {
        entry->peer_link.Save();
      }
      if (entry->room.is_valid()) {
        entry->room.Save();
        for (auto& s : entry->room->link_sync_states) {
          if (s.is_valid()) {
            s.Save();
          }
        }
      }
    }
  }
}

std::size_t CountStorageObjects(ae::RamDomainStorage const& storage) {
  std::size_t count = 0;
  for (auto const& [id, data] : storage.state) {
    (void)id;
    (void)data;
    ++count;
  }
  return count;
}

}  // namespace

// Protocol: endpoint admission, creator election, binding-before-ACK, restart,
// duplicate initial, and private-field isolation (not ChatSession/UI).
void TestProtocolHeadlessSyncCoverage() {
  MemoryNetwork network;

  ae::RamDomainStorage storage_a;
  ae::RamDomainStorage storage_b;
  ae::Domain domain_a{storage_a};
  ae::Domain domain_b{storage_b};

  std::unique_ptr<MemoryTransport> transport_a =
      std::make_unique<MemoryTransport>(network, kEndpointA);
  MemoryTransport transport_b{network, kEndpointB};

  std::unique_ptr<SharedSyncRuntime> sync_a =
      std::make_unique<SharedSyncRuntime>(domain_a, storage_a, *transport_a);
  SharedSyncRuntime sync_b{domain_b, storage_b, transport_b};

  sync_a->AllowStandaloneEventClass(MessageAddedEvent::kClassId);
  sync_b.AllowStandaloneEventClass(MessageAddedEvent::kClassId);

  auto ws_a = CreateWorkspace(domain_a, ae::ObjId{100});
  apptraverse::example::chat_demo::BindLocalEndpoint(*ws_a, kEndpointA);
  auto entry_a = apptraverse::example::chat_demo::OpenOrSelectChat(
      *ws_a, "peer-b", "Peer B");
  CHECK(entry_a.is_valid());

  apptraverse::example::chat_demo::SetDraft(*entry_a, "A's secret draft");
  apptraverse::example::chat_demo::SetScroll(
      *entry_a,
      apptraverse::example::chat_demo::ScrollAnchor{
          .follow_tail = false, .offset_from_message_top = 42.0});
  apptraverse::example::chat_demo::SetDesktopBounds(
      *ws_a, apptraverse::example::chat_demo::DesktopBounds{
                 .valid = true,
                 .x = 10,
                 .y = 20,
                 .width = 800,
                 .height = 600,
                 .maximized = false});

  auto ws_b = CreateWorkspace(domain_b, ae::ObjId{200});
  apptraverse::example::chat_demo::BindLocalEndpoint(*ws_b, kEndpointB);
  auto entry_b = apptraverse::example::chat_demo::OpenOrSelectChat(
      *ws_b, "peer-a", "Peer A");
  CHECK(entry_b.is_valid());

  CHECK(std::string(kEndpointA) < std::string(kEndpointB));

  ae::ObjId const generated_room_id = ae::ObjId::GenerateUnique();
  auto room_a = CreateRoom(domain_a, generated_room_id);
  auto link_local_a =
      CreateMemoryLink(domain_a, ae::ObjId::GenerateUnique(), kEndpointA);
  auto link_remote_b =
      CreateMemoryLink(domain_a, ae::ObjId::GenerateUnique(), kEndpointB);

  room_a->AddShare(link_local_a, ShareAccess::ReadWrite);
  room_a->AddShare(link_remote_b, ShareAccess::ReadWrite);
  apptraverse::example::chat_demo::BindChat(*entry_a, link_remote_b, room_a);
  SaveWorkspaceGraph(ws_a);
  sync_a->RegisterNode(room_a);

  sync_b.ExpectInitialNodeFromEndpoint(kEndpointA, ChatRoom::kClassId);

  bool callback_invoked = false;
  bool binding_existed_before_ack = false;

  sync_b.SetInitialNodeImportedCallback(
      [&](std::string const& source, SharedNode::ptr imported) -> bool {
        callback_invoked = true;
        CHECK(source == kEndpointA);
        CHECK(imported.is_valid());
        CHECK(imported.id() == generated_room_id);

        auto imported_room = ChatRoom::ptr::MakeFromThis(
            static_cast<ChatRoom*>(&*imported));
        CHECK(imported_room.is_valid());

        apptraverse::Link::ptr found_link;
        for (auto const& share : imported_room->shares) {
          if (share.link.is_valid() &&
              share.link->EndpointUid() == kEndpointA) {
            found_link = share.link;
            break;
          }
        }
        CHECK(found_link.is_valid());

        bool const bound = apptraverse::example::chat_demo::BindChat(
            *entry_b, found_link, imported_room, [&]() {
              SaveWorkspaceGraph(ws_b);
            });
        CHECK(bound);

        binding_existed_before_ack =
            (entry_b->room.id() == generated_room_id &&
             entry_b->peer_link.id() == found_link.id());
        return true;
      });

  ae::ObjId share_to_b;
  for (auto const& s : room_a->shares) {
    if (s.link.is_valid() && s.link->EndpointUid() == kEndpointB) {
      share_to_b = s.share_id;
      break;
    }
  }
  CHECK(share_to_b.is_valid());

  sync_a->SyncInitialState(generated_room_id, share_to_b);
  CHECK(network.PendingCount(kEndpointA, kEndpointB) == 1);

  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(callback_invoked);
  CHECK(binding_existed_before_ack);

  CHECK(network.PendingCount(kEndpointB, kEndpointA) == 1);
  CHECK(network.DeliverNext(kEndpointB, kEndpointA));

  auto room_b = entry_b->room;
  CHECK(room_b.is_valid());
  CHECK(room_b.id() == generated_room_id);
  CHECK(room_b->shares.size() == 2);

  CHECK(entry_b->draft.empty());
  CHECK(entry_b->scroll.follow_tail == true);
  CHECK(ws_b->desktop_bounds.valid == false);

  // Second initial for an existing room: valid destination share_id, no ACK,
  // no storage growth.
  {
    ae::RamDomainStorage fake_storage;
    ae::Domain fake_domain{fake_storage};
    auto fake_room = CreateRoom(fake_domain, generated_room_id);
    auto l1 = CreateMemoryLink(fake_domain, ae::ObjId{991}, kEndpointA);
    auto l2 = CreateMemoryLink(fake_domain, ae::ObjId{992}, kEndpointB);
    fake_room->AddShare(l1, ShareAccess::ReadWrite);
    fake_room->AddShare(l2, ShareAccess::ReadWrite);

    ae::ObjId fake_share_to_b;
    for (auto const& s : fake_room->shares) {
      if (s.link.is_valid() && s.link->EndpointUid() == kEndpointB) {
        fake_share_to_b = s.share_id;
        break;
      }
    }
    CHECK(fake_share_to_b.is_valid());

    auto frozen = FreezeNetworkSharedNodeState(*fake_room);
    NodeStateFrame fake_frame{
        .packet_id = ae::ObjId{888},
        .target_node_id = generated_room_id,
        .destination_share_id = fake_share_to_b,
        .payload = std::move(frozen.payload),
    };
    auto const storage_before = CountStorageObjects(storage_b);
    transport_a->Send(kEndpointB, EncodeNodeStateFrame(fake_frame));
    CHECK(network.PendingCount(kEndpointA, kEndpointB) == 1);

    std::size_t const before_shares = room_b->shares.size();
    CHECK(network.DeliverNext(kEndpointA, kEndpointB));
    CHECK(room_b->shares.size() == before_shares);
    CHECK(network.PendingCount(kEndpointB, kEndpointA) == 0);
    CHECK(CountStorageObjects(storage_b) == storage_before);
  }

  apptraverse::example::chat_demo::SetDraft(*entry_a, "Queued message 1");
  auto msg1_id = apptraverse::example::chat_demo::SubmitDraft(
      *ws_a, *entry_a, 1000, [&]() { SaveWorkspaceGraph(ws_a); });
  CHECK(!msg1_id.origin_uid.empty());
  CHECK(room_a->messages.size() == 1);

  sync_a.reset();
  transport_a.reset();

  {
    ae::Domain reloaded_domain_a{storage_a};
    auto reloaded_ws_a = ChatWorkspace::ptr::Declare(
        ae::CreateWith{reloaded_domain_a}.with_id(ae::ObjId{100}));
    reloaded_ws_a.Load();
    CHECK(reloaded_ws_a.is_valid());
    CHECK(reloaded_ws_a->chats.size() == 1);

    auto reloaded_entry_a = reloaded_ws_a->chats[0];
    reloaded_entry_a.Load();
    CHECK(reloaded_entry_a->room.is_valid());
    CHECK(reloaded_entry_a->room.id() == generated_room_id);
    reloaded_entry_a->room.Load();
    CHECK(reloaded_entry_a->room->messages.size() == 1);
    CHECK(reloaded_entry_a->room->messages[0].text == "Queued message 1");

    MemoryTransport reloaded_transport_a{network, kEndpointA};
    SharedSyncRuntime reloaded_sync_a{reloaded_domain_a, storage_a,
                                      reloaded_transport_a};
    reloaded_sync_a.AllowStandaloneEventClass(MessageAddedEvent::kClassId);
    reloaded_sync_a.RegisterNode(reloaded_entry_a->room);

    reloaded_sync_a.SyncNextEvent(generated_room_id, share_to_b);
    CHECK(network.PendingCount(kEndpointA, kEndpointB) == 1);
    CHECK(network.DeliverNext(kEndpointA, kEndpointB));
    CHECK(room_b->messages.size() == 1);
    CHECK(room_b->messages[0].text == "Queued message 1");
    CHECK(network.PendingCount(kEndpointB, kEndpointA) == 1);
    CHECK(network.DeliverNext(kEndpointB, kEndpointA));
  }
}

void TestProtocolFailedBindingBlocksAckUntilSuccess() {
  MemoryNetwork network;

  ae::RamDomainStorage storage_a;
  ae::RamDomainStorage storage_b;
  ae::Domain domain_a{storage_a};
  ae::Domain domain_b{storage_b};

  MemoryTransport transport_a{network, kEndpointA};
  MemoryTransport transport_b{network, kEndpointB};
  SharedSyncRuntime sync_a{domain_a, storage_a, transport_a};
  SharedSyncRuntime sync_b{domain_b, storage_b, transport_b};
  sync_a.AllowStandaloneEventClass(MessageAddedEvent::kClassId);
  sync_b.AllowStandaloneEventClass(MessageAddedEvent::kClassId);

  ae::ObjId const room_id = ae::ObjId::GenerateUnique();
  auto room_a = CreateRoom(domain_a, room_id);
  auto link_local_a =
      CreateMemoryLink(domain_a, ae::ObjId::GenerateUnique(), kEndpointA);
  auto link_remote_b =
      CreateMemoryLink(domain_a, ae::ObjId::GenerateUnique(), kEndpointB);
  room_a->AddShare(link_local_a, ShareAccess::ReadWrite);
  room_a->AddShare(link_remote_b, ShareAccess::ReadWrite);
  sync_a.RegisterNode(room_a);

  sync_b.ExpectInitialNodeFromEndpoint(kEndpointA, ChatRoom::kClassId);

  int callback_count = 0;
  sync_b.SetInitialNodeImportedCallback(
      [&](std::string const& source, SharedNode::ptr imported) -> bool {
        CHECK(source == kEndpointA);
        CHECK(imported.is_valid());
        ++callback_count;
        return callback_count >= 2;
      });

  ae::ObjId share_to_b;
  for (auto const& s : room_a->shares) {
    if (s.link.is_valid() && s.link->EndpointUid() == kEndpointB) {
      share_to_b = s.share_id;
      break;
    }
  }
  CHECK(share_to_b.is_valid());

  sync_a.SyncInitialState(room_id, share_to_b);
  CHECK(network.PendingCount(kEndpointA, kEndpointB) == 1);
  CHECK(network.DuplicateNext(kEndpointA, kEndpointB));
  CHECK(network.PendingCount(kEndpointA, kEndpointB) == 2);

  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(callback_count == 1);
  CHECK(network.PendingCount(kEndpointB, kEndpointA) == 0);

  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(callback_count == 2);
  CHECK(network.PendingCount(kEndpointB, kEndpointA) == 1);
  CHECK(network.DeliverNext(kEndpointB, kEndpointA));
}

void TestProtocolLostAckIdenticalRetryOneMessage() {
  MemoryNetwork network;

  ae::RamDomainStorage storage_a;
  ae::RamDomainStorage storage_b;
  ae::Domain domain_a{storage_a};
  ae::Domain domain_b{storage_b};

  MemoryTransport transport_a{network, kEndpointA};
  MemoryTransport transport_b{network, kEndpointB};
  SharedSyncRuntime sync_a{domain_a, storage_a, transport_a};
  SharedSyncRuntime sync_b{domain_b, storage_b, transport_b};
  sync_a.AllowStandaloneEventClass(MessageAddedEvent::kClassId);
  sync_b.AllowStandaloneEventClass(MessageAddedEvent::kClassId);

  ae::ObjId const room_id = ae::ObjId{9001};
  auto ws_a = CreateWorkspace(domain_a, ae::ObjId{9010});
  apptraverse::example::chat_demo::BindLocalEndpoint(*ws_a, kEndpointA);
  auto entry_a = apptraverse::example::chat_demo::OpenOrSelectChat(
      *ws_a, "peer-b", "Peer B");

  auto room_a = CreateRoom(domain_a, room_id);
  auto link_a = CreateMemoryLink(domain_a, ae::ObjId{9002}, kEndpointA);
  auto link_b = CreateMemoryLink(domain_a, ae::ObjId{9003}, kEndpointB);
  room_a->AddShare(link_a, ShareAccess::ReadWrite);
  room_a->AddShare(link_b, ShareAccess::ReadWrite);
  apptraverse::example::chat_demo::BindChat(*entry_a, link_b, room_a);
  SaveWorkspaceGraph(ws_a);
  sync_a.RegisterNode(room_a);

  ae::ObjId share_to_b;
  for (auto const& share : room_a->shares) {
    if (share.link.is_valid() && share.link->EndpointUid() == kEndpointB) {
      share_to_b = share.share_id;
      break;
    }
  }
  CHECK(share_to_b.is_valid());

  sync_b.ExpectInitialNode(room_id);
  sync_a.SyncInitialState(room_id, share_to_b);
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(network.DeliverNext(kEndpointB, kEndpointA));

  auto imported_room_b = sync_b.FindNode(room_id);
  CHECK(imported_room_b.is_valid());
  auto room_b = ChatRoom::ptr::MakeFromThis(
      static_cast<ChatRoom*>(&*imported_room_b));

  apptraverse::example::chat_demo::SetDraft(*entry_a, "once-only");
  auto msg_id = apptraverse::example::chat_demo::SubmitDraft(
      *ws_a, *entry_a, 5000, [&]() { SaveWorkspaceGraph(ws_a); });
  CHECK(!msg_id.origin_uid.empty());
  CHECK(room_a->messages.size() == 1);

  sync_a.SyncNextEvent(room_id, share_to_b);
  CHECK(network.PendingCount(kEndpointA, kEndpointB) == 1);
  CHECK(network.DuplicateNext(kEndpointA, kEndpointB));
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));

  CHECK(room_b->messages.size() == 1);
  CHECK(room_b->messages[0].text == "once-only");

  // Duplicate event delivery must not duplicate the visible timeline; ACK
  // retry (lost ACK + identical resend) is likewise idempotent.
  std::size_t const acks = network.PendingCount(kEndpointB, kEndpointA);
  CHECK(acks >= 1);
  for (std::size_t i = 0; i < acks; ++i) {
    CHECK(network.DeliverNext(kEndpointB, kEndpointA));
  }
  CHECK(room_a->messages.size() == 1);
  CHECK(room_b->messages.size() == 1);
}

void TestProtocolUnauthorizedAndWrongClassRejection() {
  MemoryNetwork network;

  ae::RamDomainStorage storage_b;
  ae::Domain domain_b{storage_b};
  MemoryTransport transport_b{network, kEndpointB};
  SharedSyncRuntime sync_b{domain_b, storage_b, transport_b};
  sync_b.AllowStandaloneEventClass(MessageAddedEvent::kClassId);

  sync_b.ExpectInitialNodeFromEndpoint(kEndpointA, ChatRoom::kClassId);

  // Unknown source.
  {
    ae::RamDomainStorage storage_eve;
    ae::Domain domain_eve{storage_eve};
    MemoryTransport transport_eve{network, kEndpointEve};

    auto room_eve = CreateRoom(domain_eve, ae::ObjId{7001});
    auto link_eve = CreateMemoryLink(domain_eve, ae::ObjId{7002}, kEndpointEve);
    auto link_b = CreateMemoryLink(domain_eve, ae::ObjId{7003}, kEndpointB);
    room_eve->AddShare(link_eve, ShareAccess::ReadWrite);
    room_eve->AddShare(link_b, ShareAccess::ReadWrite);

    ae::ObjId share_to_b;
    for (auto const& s : room_eve->shares) {
      if (s.link.is_valid() && s.link->EndpointUid() == kEndpointB) {
        share_to_b = s.share_id;
        break;
      }
    }
    CHECK(share_to_b.is_valid());

    auto frozen = FreezeNetworkSharedNodeState(*room_eve);
    NodeStateFrame frame{
        .packet_id = ae::ObjId{7004},
        .target_node_id = ae::ObjId{7001},
        .destination_share_id = share_to_b,
        .payload = std::move(frozen.payload),
    };
    transport_eve.Send(kEndpointB, EncodeNodeStateFrame(frame));
    CHECK(network.PendingCount(kEndpointEve, kEndpointB) == 1);

    auto const storage_before = CountStorageObjects(storage_b);
    CHECK(network.DeliverNext(kEndpointEve, kEndpointB));
    CHECK(sync_b.FindNode(ae::ObjId{7001}).is_valid() == false);
    CHECK(network.PendingCount(kEndpointB, kEndpointEve) == 0);
    CHECK(CountStorageObjects(storage_b) == storage_before);
  }

  // Authorized endpoint, wrong root class: valid SharedNode sibling, not
  // ChatRoom. Payload uses NodeState / network graph encoding.
  {
    apptraverse::example::shared_node::EnsureSharedNodeDemoRegistration();
    ae::RamDomainStorage storage_a;
    ae::Domain domain_a{storage_a};
    MemoryTransport transport_a{network, kEndpointA};

    auto wrong_node = SharedValueNode::ptr::Create(
        ae::CreateWith{domain_a}.with_id(ae::ObjId{8001}));
    InitializeRuntimeNode(*wrong_node);
    auto link_a = MemoryLink::ptr::Create(
        ae::CreateWith{domain_a}.with_id(ae::ObjId{8002}));
    link_a->endpoint_uid = kEndpointA;
    InitializeRuntimeNode(*link_a);
    auto link_b_peer = MemoryLink::ptr::Create(
        ae::CreateWith{domain_a}.with_id(ae::ObjId{8003}));
    link_b_peer->endpoint_uid = kEndpointB;
    InitializeRuntimeNode(*link_b_peer);
    wrong_node->AddShare(link_a, ShareAccess::ReadWrite);
    wrong_node->AddShare(link_b_peer, ShareAccess::ReadWrite);

    ae::ObjId share_to_b;
    for (auto const& s : wrong_node->shares) {
      if (s.link.is_valid() && s.link->EndpointUid() == kEndpointB) {
        share_to_b = s.share_id;
        break;
      }
    }
    CHECK(share_to_b.is_valid());

    auto const storage_before = CountStorageObjects(storage_b);
    transport_a.Send(
        kEndpointB,
        EncodeNodeStateFrame(NodeStateFrame{
            .packet_id = ae::ObjId{8004},
            .target_node_id = ae::ObjId{8001},
            .destination_share_id = share_to_b,
            .payload = SerializeNetworkSharedObjectGraph(*wrong_node),
        }));
    CHECK(network.PendingCount(kEndpointA, kEndpointB) == 1);
    CHECK(network.DeliverNext(kEndpointA, kEndpointB));
    CHECK(sync_b.FindNode(ae::ObjId{8001}).is_valid() == false);
    CHECK(storage_b.Enumerate(ae::ObjId{8001}).empty());
    CHECK(network.PendingCount(kEndpointB, kEndpointA) == 0);
    CHECK(CountStorageObjects(storage_b) == storage_before);
  }
}

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::example::chat_demo::EnsureChatDemoModelRegistration();
  apptraverse::example::shared_node::EnsureSharedNodeDemoRegistration();

  std::cout << "Running shared_sync_protocol_test...\n";
  apptraverse::test::TestProtocolHeadlessSyncCoverage();
  std::cout << "  Protocol headless sync coverage passed\n";
  apptraverse::test::TestProtocolFailedBindingBlocksAckUntilSuccess();
  std::cout << "  Protocol failed-binding ACK gate passed\n";
  apptraverse::test::TestProtocolLostAckIdenticalRetryOneMessage();
  std::cout << "  Protocol lost-ACK identical retry passed\n";
  apptraverse::test::TestProtocolUnauthorizedAndWrongClassRejection();
  std::cout << "  Protocol unauthorized/wrong-class rejection passed\n";
  std::cout << "shared_sync_protocol_test passed!\n";
  return 0;
}
