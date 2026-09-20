#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj_id.h"

#include "apptraverse/memory_transport.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/shared_network_graph.h"
#include "apptraverse/shared_node.h"
#include "apptraverse/shared_sync_runtime.h"

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

char const* const kEndpointA = "chat-a";
char const* const kEndpointB = "chat-b";

ChatWorkspace::ptr CreateWorkspace(ae::Domain& domain, ae::ObjId id) {
  auto ws = ChatWorkspace::ptr::Create(ae::CreateWith{domain}.with_id(id));
  InitializeRuntimeNode(*ws);
  return ws;
}

ChatRoom::ptr CreateRoom(ae::Domain& domain, ae::ObjId id) {
  auto room = ChatRoom::ptr::Create(ae::CreateWith{domain}.with_id(id));
  InitializeRuntimeNode(*room);
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

void TestTwoWayChatOverMemoryNetwork() {
  MemoryNetwork network;

  // Storage and Domains
  ae::RamDomainStorage storage_a;
  ae::RamDomainStorage storage_b;
  ae::Domain domain_a{storage_a};
  ae::Domain domain_b{storage_b};

  // Transports
  MemoryTransport transport_a{network, kEndpointA};
  MemoryTransport transport_b{network, kEndpointB};

  // Sync runtimes
  SharedSyncRuntime sync_a{domain_a, storage_a, transport_a};
  SharedSyncRuntime sync_b{domain_b, storage_b, transport_b};

  sync_a.AllowStandaloneEventClass(MessageAddedEvent::kClassId);
  sync_b.AllowStandaloneEventClass(MessageAddedEvent::kClassId);

  // 1. Build A's chat
  ae::ObjId const ws_a_id{100};
  ae::ObjId const entry_a_id{101};
  ae::ObjId const link_a_id{102};
  ae::ObjId const link_b_id{103};
  ae::ObjId const room_id{104};

  auto ws_a = CreateWorkspace(domain_a, ws_a_id);
  BindLocalEndpoint(*ws_a, kEndpointA);

  auto entry_a = OpenOrSelectChat(*ws_a, "peer-b", [] {});
  CHECK(entry_a.is_valid());

  auto link_local_a = CreateMemoryLink(domain_a, link_a_id, kEndpointA);
  auto link_remote_b = CreateMemoryLink(domain_a, link_b_id, kEndpointB);
  auto room_a = CreateRoom(domain_a, room_id);

  room_a->InstallLocalShare(link_local_a, ShareAccess::ReadWrite);
  room_a->InstallLocalShare(link_remote_b, ShareAccess::ReadWrite);

  BindChat(*entry_a, link_remote_b, room_a);

  SaveWorkspaceGraph(ws_a);
  sync_a.RegisterNode(room_a);

  // Find share_to_b on room_a
  ae::ObjId share_to_b;
  for (auto const& share : room_a->shares) {
    if (share.link.is_valid() && share.link->EndpointUid() == kEndpointB) {
      share_to_b = share.share_id;
      break;
    }
  }
  CHECK(share_to_b.is_valid());

  // 2. Initial Snapshot: A -> B
  sync_b.ExpectInitialNode(room_id);
  sync_a.SyncInitialState(room_id, share_to_b);

  CHECK(network.PendingCount(kEndpointA, kEndpointB) == 1);
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));

  // B must have imported room
  auto imported_room_b = sync_b.FindNode(room_id);
  CHECK(imported_room_b.is_valid());
  auto room_b = ChatRoom::ptr::MakeFromThis(static_cast<ChatRoom*>(&*imported_room_b));

  // B sends ACK back to A
  CHECK(network.PendingCount(kEndpointB, kEndpointA) == 1);
  CHECK(network.DeliverNext(kEndpointB, kEndpointA));

  // Find shares on B
  ae::ObjId share_to_a_on_b;
  for (auto const& share : room_b->shares) {
    if (share.link.is_valid() && share.link->EndpointUid() == kEndpointA) {
      share_to_a_on_b = share.share_id;
      break;
    }
  }
  CHECK(share_to_a_on_b.is_valid());

  // Verify B's source share back to A is ALREADY Complete!
  auto const b_source_sync_idx = room_b->FindLinkSyncIndexForShare(share_to_a_on_b);
  CHECK(b_source_sync_idx < room_b->link_sync_states.size());
  auto b_source_state = room_b->link_sync_states[b_source_sync_idx];
  CHECK(b_source_state->GetInitialSyncPhase() == InitialSyncPhase::Complete);

  // Set up B's local workspace and entry
  ae::ObjId const ws_b_id{200};
  auto ws_b = CreateWorkspace(domain_b, ws_b_id);
  BindLocalEndpoint(*ws_b, kEndpointB);
  auto entry_b = OpenOrSelectChat(*ws_b, "peer-a", [] {});

  auto link_remote_a_on_b = CreateMemoryLink(domain_b, link_a_id, kEndpointA);
  BindChat(*entry_b, link_remote_a_on_b, room_b);
  SaveWorkspaceGraph(ws_b);

  // 3. Message from A -> B
  SetDraft(*entry_a, "Hello from A");
  auto msg_a_id = SubmitDraft(*ws_a, *entry_a, 1000);
  CHECK(!msg_a_id.origin_uid.empty());
  CHECK(room_a->messages.size() == 1);
  CHECK(room_a->messages[0].text == "Hello from A");

  sync_a.SyncNextEvent(room_id, share_to_b);
  CHECK(network.PendingCount(kEndpointA, kEndpointB) == 1);
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));

  // B receives and applies message
  CHECK(room_b->messages.size() == 1);
  CHECK(room_b->messages[0].text == "Hello from A");
  CHECK(room_b->messages[0].id == msg_a_id);

  // Deliver ACK from B to A
  CHECK(network.PendingCount(kEndpointB, kEndpointA) == 1);
  CHECK(network.DeliverNext(kEndpointB, kEndpointA));

  // 4. Message from B -> A (immediate reply!)
  SetDraft(*entry_b, "Reply from B");
  auto msg_b_id = SubmitDraft(*ws_b, *entry_b, 2000);
  CHECK(!msg_b_id.origin_uid.empty());
  CHECK(room_b->messages.size() == 2);
  CHECK(room_b->messages[1].text == "Reply from B");

  sync_b.SyncNextEvent(room_id, share_to_a_on_b);
  CHECK(network.PendingCount(kEndpointB, kEndpointA) == 1);
  CHECK(network.DeliverNext(kEndpointB, kEndpointA));

  // A receives reply
  CHECK(room_a->messages.size() == 2);
  CHECK(room_a->messages[1].text == "Reply from B");
  CHECK(room_a->messages[1].id == msg_b_id);

  // Deliver ACK from A to B
  CHECK(network.PendingCount(kEndpointA, kEndpointB) == 1);
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));

  // 5. Test exact ACK / retry behavior on lost ACK
  SetDraft(*entry_a, "Second message from A");
  auto msg_a2_id = SubmitDraft(*ws_a, *entry_a, 3000);
  sync_a.SyncNextEvent(room_id, share_to_b);

  // Deliver packet A -> B
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(room_b->messages.size() == 3);

  // Drop ACK from B to A!
  CHECK(network.PendingCount(kEndpointB, kEndpointA) == 1);
  network.DropNext(kEndpointB, kEndpointA);
  CHECK(network.PendingCount(kEndpointB, kEndpointA) == 0);

  // A resends exact packet
  sync_a.SyncNextEvent(room_id, share_to_b);
  CHECK(network.PendingCount(kEndpointA, kEndpointB) == 1);
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));

  // B recognizes duplicate, does not duplicate message in room
  CHECK(room_b->messages.size() == 3);

  // B re-sends ACK; this time deliver it
  CHECK(network.PendingCount(kEndpointB, kEndpointA) == 1);
  CHECK(network.DeliverNext(kEndpointB, kEndpointA));

  // 6. Test restart behavior: reload B from persisted storage
  SaveWorkspaceGraph(ws_b);
  {
    ae::Domain reloaded_domain_b{storage_b};
    auto reloaded_ws = ChatWorkspace::ptr::Declare(
        ae::CreateWith{reloaded_domain_b}.with_id(ws_b_id));
    reloaded_ws.Load();
    CHECK(reloaded_ws->chats.size() == 1);
    auto reloaded_entry = reloaded_ws->chats[0];
    reloaded_entry.Load();
    CHECK(reloaded_entry->room.is_valid());
    reloaded_entry->room.Load();
    CHECK(reloaded_entry->room->messages.size() == 3);
    CHECK(reloaded_entry->room->messages[0].text == "Hello from A");
    CHECK(reloaded_entry->room->messages[1].text == "Reply from B");
    CHECK(reloaded_entry->room->messages[2].text == "Second message from A");
  }

  // 7. Verify mismatched Event payload is rejected without ACK
  {
    // Construct a MessageAddedEvent with timestamp mismatch
    auto bad_ev = MessageAddedEvent::ptr::Create(ae::CreateWith{domain_a});
    SharedEventId const bad_id{.origin_uid = kEndpointA, .origin_sequence = 99};
    bad_ev->message = MessageValue{
        .id = bad_id,
        .timestamp_us = 99999, // Mismatched timestamp
        .text = "Tampered message",
    };
    std::vector<std::uint8_t> payload;
    CHECK(FreezeEventPayload(*bad_ev, payload));

    EventFrame const tampered_frame{
        .packet_id = ae::ObjId{999},
        .target_node_id = room_id,
        .destination_share_id = share_to_a_on_b,
        .identity = bad_id,
        .timestamp_us = 5000, // Frame says 5000, payload says 99999!
        .event_class_id = MessageAddedEvent::kClassId,
        .payload = payload,
    };

    transport_a.Send(kEndpointB, EncodeEventFrame(tampered_frame));
    CHECK(network.PendingCount(kEndpointA, kEndpointB) == 1);
    CHECK(network.DeliverNext(kEndpointA, kEndpointB));

    // B must reject: room messages unchanged, no ACK sent back to A
    CHECK(room_b->messages.size() == 3);
    CHECK(network.PendingCount(kEndpointB, kEndpointA) == 0);
  }
}

}  // namespace
}  // namespace apptraverse::example::chat_demo

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::example::chat_demo::EnsureChatDemoModelRegistration();

  std::cout << "Running apptraverse_chat_demo_sync_test...\n";
  apptraverse::example::chat_demo::TestTwoWayChatOverMemoryNetwork();
  std::cout << "apptraverse_chat_demo_sync_test passed!\n";
  return 0;
}
