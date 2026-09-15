#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
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

#include "aether_link.h"
#include "chat_commands.h"
#include "chat_model.h"

namespace apptraverse::example::chat_demo {
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

AetherLink::ptr CreateAetherLink(ae::Domain& domain, ae::ObjId id,
                                 std::string endpoint) {
  auto link = AetherLink::ptr::Create(ae::CreateWith{domain}.with_id(id));
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

// 1. Two sessions discover room ID through endpoint-based admission (not a fixed room ID known in advance)
// 2. Both start from empty independent profiles
// 3. Only one side creates the room (canonical UID: kEndpointA < kEndpointB => A is creator)
// 4. Workspace binding exists before receiver sends ACK
// 5. Restart restores the SAME room and Link
// 6. Known room rejects a new initial snapshot
// 7. Queued message survives restart and sends after peer returns
// 8. Private draft/scroll/bounds never enter room network payload
void TestHeadlessSyncCoverage() {
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

  // Setup A's workspace
  auto ws_a = CreateWorkspace(domain_a, ae::ObjId{100});
  BindLocalEndpoint(*ws_a, kEndpointA);
  auto entry_a = OpenOrSelectChat(*ws_a, "peer-b", "Peer B");
  CHECK(entry_a.is_valid());

  // Private fields on A
  SetDraft(*entry_a, "A's secret draft");
  SetScroll(*entry_a, ScrollAnchor{.follow_tail = false, .offset_from_message_top = 42.0});
  SetDesktopBounds(*ws_a, DesktopBounds{.valid = true, .x = 10, .y = 20, .width = 800, .height = 600, .maximized = false});

  // Setup B's workspace
  auto ws_b = CreateWorkspace(domain_b, ae::ObjId{200});
  BindLocalEndpoint(*ws_b, kEndpointB);
  auto entry_b = OpenOrSelectChat(*ws_b, "peer-a", "Peer A");
  CHECK(entry_b.is_valid());

  // Bootstrap role selection: "uid-aaa-111" < "uid-bbb-222" => A is Creator!
  CHECK(std::string(kEndpointA) < std::string(kEndpointB));

  // A creates the room with generated ObjId (unknown to B)
  ae::ObjId const generated_room_id = ae::ObjId::GenerateUnique();
  auto room_a = CreateRoom(domain_a, generated_room_id);
  auto link_local_a = CreateAetherLink(domain_a, ae::ObjId::GenerateUnique(), kEndpointA);
  auto link_remote_b = CreateAetherLink(domain_a, ae::ObjId::GenerateUnique(), kEndpointB);

  room_a->AddShare(link_local_a, ShareAccess::ReadWrite);
  room_a->AddShare(link_remote_b, ShareAccess::ReadWrite);
  BindChat(*entry_a, link_remote_b, room_a);
  SaveWorkspaceGraph(ws_a);
  sync_a->RegisterNode(room_a);

  // B is Waiting side: authorizes initial node from Endpoint A for ChatRoom::kClassId
  // B does NOT know generated_room_id in advance!
  sync_b.ExpectInitialNodeFromEndpoint(kEndpointA, ChatRoom::kClassId);

  bool callback_invoked = false;
  bool binding_existed_before_ack = false;

  sync_b.SetInitialNodeImportedCallback([&](std::string const& source, SharedNode::ptr imported) -> bool {
    callback_invoked = true;
    CHECK(source == kEndpointA);
    CHECK(imported.is_valid());
    CHECK(imported.id() == generated_room_id);

    auto imported_room = ChatRoom::ptr::MakeFromThis(static_cast<ChatRoom*>(&*imported));
    CHECK(imported_room.is_valid());

    // Find remote link
    apptraverse::Link::ptr found_link;
    for (auto const& share : imported_room->shares) {
      if (share.link.is_valid() && share.link->EndpointUid() == kEndpointA) {
        found_link = share.link;
        break;
      }
    }
    CHECK(found_link.is_valid());

    bool const bound = BindChat(*entry_b, found_link, imported_room, [&]() {
      SaveWorkspaceGraph(ws_b);
    });
    CHECK(bound);

    // Verify workspace binding exists now, before ACK is sent
    binding_existed_before_ack = (entry_b->room.id() == generated_room_id &&
                                  entry_b->peer_link.id() == found_link.id());
    return true;
  });

  // Find share to B on A
  ae::ObjId share_to_b;
  for (auto const& s : room_a->shares) {
    if (s.link.is_valid() && s.link->EndpointUid() == kEndpointB) {
      share_to_b = s.share_id;
      break;
    }
  }
  CHECK(share_to_b.is_valid());

  // A sends initial snapshot
  sync_a->SyncInitialState(generated_room_id, share_to_b);
  CHECK(network.PendingCount(kEndpointA, kEndpointB) == 1);

  // Deliver snapshot to B
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(callback_invoked);
  CHECK(binding_existed_before_ack);

  // B has queued the ACK
  CHECK(network.PendingCount(kEndpointB, kEndpointA) == 1);
  CHECK(network.DeliverNext(kEndpointB, kEndpointA));

  // Verify room B is now imported and registered
  auto room_b = entry_b->room;
  CHECK(room_b.is_valid());
  CHECK(room_b.id() == generated_room_id);
  CHECK(room_b->shares.size() == 2);

  // Verify private fields of A did NOT enter network / room B
  CHECK(entry_b->draft.empty());
  CHECK(entry_b->scroll.follow_tail == true);
  CHECK(ws_b->desktop_bounds.valid == false);

  // Test Known room rejects a second new initial snapshot
  {
    ae::RamDomainStorage fake_storage;
    ae::Domain fake_domain{fake_storage};
    auto fake_room = CreateRoom(fake_domain, generated_room_id);
    auto l1 = CreateAetherLink(fake_domain, ae::ObjId{991}, kEndpointA);
    auto l2 = CreateAetherLink(fake_domain, ae::ObjId{992}, kEndpointB);
    fake_room->AddShare(l1, ShareAccess::ReadWrite);
    fake_room->AddShare(l2, ShareAccess::ReadWrite);

    auto frozen = FreezeNetworkSharedNodeState(*fake_room);
    NodeStateFrame fake_frame{
        .packet_id = ae::ObjId{888},
        .target_node_id = generated_room_id,
        .destination_share_id = l2.id(),
        .payload = std::move(frozen.payload),
    };
    transport_a->Send(kEndpointB, EncodeNodeStateFrame(fake_frame));
    CHECK(network.PendingCount(kEndpointA, kEndpointB) == 1);

    // B must reject the second initial snapshot for the existing node
    std::size_t const before_shares = room_b->shares.size();
    CHECK(network.DeliverNext(kEndpointA, kEndpointB));
    CHECK(room_b->shares.size() == before_shares);
  }

  // Submit message on A while B is offline (queued message)
  SetDraft(*entry_a, "Queued message 1");
  auto msg1_id = SubmitDraft(*ws_a, *entry_a, 1000, [&]() {
    SaveWorkspaceGraph(ws_a);
  });
  CHECK(!msg1_id.origin_uid.empty());
  CHECK(room_a->messages.size() == 1);

  // Destroy sync_a and transport_a to simulate process restart for replica A
  sync_a.reset();
  transport_a.reset();

  // Restart A from persistent storage: verify SAME room, link, and queued message survive
  {
    ae::Domain reloaded_domain_a{storage_a};
    auto reloaded_ws_a = ChatWorkspace::ptr::Declare(ae::CreateWith{reloaded_domain_a}.with_id(ae::ObjId{100}));
    reloaded_ws_a.Load();
    CHECK(reloaded_ws_a.is_valid());
    CHECK(reloaded_ws_a->chats.size() == 1);

    auto reloaded_entry_a = reloaded_ws_a->chats[0];
    reloaded_entry_a.Load();
    CHECK(reloaded_entry_a->room.is_valid());
    CHECK(reloaded_entry_a->room.id() == generated_room_id);
    CHECK(reloaded_entry_a->peer_link.is_valid());
    reloaded_entry_a->room.Load();
    CHECK(reloaded_entry_a->room->messages.size() == 1);
    CHECK(reloaded_entry_a->room->messages[0].text == "Queued message 1");

    // Reconstructed sync runtime drives the queued message to B
    MemoryTransport reloaded_transport_a{network, kEndpointA};
    SharedSyncRuntime reloaded_sync_a{reloaded_domain_a, storage_a, reloaded_transport_a};
    reloaded_sync_a.AllowStandaloneEventClass(MessageAddedEvent::kClassId);
    reloaded_sync_a.RegisterNode(reloaded_entry_a->room);

    reloaded_sync_a.SyncNextEvent(generated_room_id, share_to_b);
    CHECK(network.PendingCount(kEndpointA, kEndpointB) == 1);

    // Deliver to B
    CHECK(network.DeliverNext(kEndpointA, kEndpointB));
    CHECK(room_b->messages.size() == 1);
    CHECK(room_b->messages[0].text == "Queued message 1");

    // B sends ACK back to A
    CHECK(network.PendingCount(kEndpointB, kEndpointA) == 1);
    CHECK(network.DeliverNext(kEndpointB, kEndpointA));
  }
}

// Unauthorized source cannot create a room; wrong root class rejected before writes
void TestUnauthorizedAndWrongClassRejection() {
  MemoryNetwork network;

  ae::RamDomainStorage storage_b;
  ae::Domain domain_b{storage_b};
  MemoryTransport transport_b{network, kEndpointB};
  SharedSyncRuntime sync_b{domain_b, storage_b, transport_b};
  sync_b.AllowStandaloneEventClass(MessageAddedEvent::kClassId);

  // Expect room ONLY from authorized endpoint A
  sync_b.ExpectInitialNodeFromEndpoint(kEndpointA, ChatRoom::kClassId);

  // 1. Eve sends a snapshot
  {
    ae::RamDomainStorage storage_eve;
    ae::Domain domain_eve{storage_eve};
    MemoryTransport transport_eve{network, kEndpointEve};

    auto room_eve = CreateRoom(domain_eve, ae::ObjId{7001});
    auto link_eve = CreateAetherLink(domain_eve, ae::ObjId{7002}, kEndpointEve);
    auto link_b = CreateAetherLink(domain_eve, ae::ObjId{7003}, kEndpointB);
    room_eve->AddShare(link_eve, ShareAccess::ReadWrite);
    room_eve->AddShare(link_b, ShareAccess::ReadWrite);

    auto frozen = FreezeNetworkSharedNodeState(*room_eve);
    NodeStateFrame frame{
        .packet_id = ae::ObjId{7004},
        .target_node_id = ae::ObjId{7001},
        .destination_share_id = link_b.id(),
        .payload = std::move(frozen.payload),
    };
    transport_eve.Send(kEndpointB, EncodeNodeStateFrame(frame));
    CHECK(network.PendingCount(kEndpointEve, kEndpointB) == 1);

    // Deliver: B must reject because Eve is unauthorized
    CHECK(network.DeliverNext(kEndpointEve, kEndpointB));
    CHECK(sync_b.FindNode(ae::ObjId{7001}).is_valid() == false);
    CHECK(network.PendingCount(kEndpointB, kEndpointEve) == 0); // No ACK
  }

  // 2. Authorized endpoint A sends WRONG root class (e.g. ChatWorkspace instead of ChatRoom)
  {
    ae::RamDomainStorage storage_a;
    ae::Domain domain_a{storage_a};
    MemoryTransport transport_a{network, kEndpointA};

    auto wrong_root = CreateWorkspace(domain_a, ae::ObjId{8001});
    // Serialize object graph as payload
    ByteSink sink;
    SerializeObjectGraphToBuffer(*wrong_root, sink);

    NodeStateFrame frame{
        .packet_id = ae::ObjId{8002},
        .target_node_id = ae::ObjId{8001},
        .destination_share_id = ae::ObjId{8003},
        .payload = std::move(sink.bytes),
    };
    transport_a.Send(kEndpointB, EncodeNodeStateFrame(frame));
    CHECK(network.PendingCount(kEndpointA, kEndpointB) == 1);

    // Deliver: B must reject before writes because root class is not ChatRoom
    CHECK(network.DeliverNext(kEndpointA, kEndpointB));
    CHECK(sync_b.FindNode(ae::ObjId{8001}).is_valid() == false);
    CHECK(storage_b.Enumerate(ae::ObjId{8001}).empty());
    CHECK(network.PendingCount(kEndpointB, kEndpointA) == 0); // No ACK
  }
}

}  // namespace
}  // namespace apptraverse::example::chat_demo

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::example::chat_demo::EnsureChatDemoModelRegistration();
  apptraverse::example::chat_demo::EnsureAetherLinkRegistration();

  std::cout << "Running chat_session_integration_test...\n";
  apptraverse::example::chat_demo::TestHeadlessSyncCoverage();
  std::cout << "  Headless sync coverage passed!\n";
  apptraverse::example::chat_demo::TestUnauthorizedAndWrongClassRejection();
  std::cout << "  Unauthorized and wrong class rejection passed!\n";
  std::cout << "chat_session_integration_test passed!\n";
  return 0;
}
