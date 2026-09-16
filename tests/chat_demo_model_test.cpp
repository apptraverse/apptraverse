#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/idomain_storage.h"
#include "aether-objects/obj/obj_id.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/link.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/shared_event_id.h"
#include "apptraverse/shared_network_graph.h"
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

ChatWorkspace::ptr CreateWorkspace(ae::Domain& domain,
                                   ae::ObjId id = ae::ObjId{1}) {
  auto ws = ChatWorkspace::ptr::Create(ae::CreateWith{domain}.with_id(id));
  InitializeRuntimeNode(*ws);
  return ws;
}

ChatRoom::ptr CreateRoom(ae::Domain& domain, ae::ObjId id = ae::ObjId{2}) {
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

// Scenario 1: Open two Admin IDs; get two entries.
void TestScenario1_OpenTwoAdminIds() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto ws = CreateWorkspace(domain);

  auto e1 = OpenOrSelectChat(*ws, "peer-100", [] {});
  auto e2 = OpenOrSelectChat(*ws, "peer-200", [] {});

  CHECK(e1.is_valid());
  CHECK(e2.is_valid());
  CHECK(e1.id() != e2.id());
  CHECK(ws->chats.size() == 2);
  CHECK(ws->chats[0].id() == e1.id());
  CHECK(ws->chats[1].id() == e2.id());
  CHECK(ws->selected_chat_id == e2.id());
}

// Scenario 2: Open the first ID again; same entry and room, no duplicate.
void TestScenario2_OpenFirstIdAgain() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto ws = CreateWorkspace(domain);

  auto e1 = OpenOrSelectChat(*ws, "peer-100", [] {});
  auto e2 = OpenOrSelectChat(*ws, "peer-200", [] {});
  CHECK(ws->selected_chat_id == e2.id());

  // Opening peer-100 again (with surrounding whitespace to test trimming)
  auto e1_again = OpenOrSelectChat(*ws, "  peer-100  ", [] {});
  CHECK(ws->chats.size() == 2);
  CHECK(e1_again.id() == e1.id());
  CHECK(ws->selected_chat_id == e1.id());
}

// Scenario 3: Entry can persist while unresolved, with no Link/Room.
void TestScenario3_EntryPersistWhileUnresolved() {
  ae::RamDomainStorage storage;
  ae::ObjId const ws_id{10};
  {
    ae::Domain domain{storage};
    auto ws = CreateWorkspace(domain, ws_id);
    auto e = OpenOrSelectChat(*ws, "peer-300", [] {});
    CHECK(e.is_valid());
    CHECK(!e->peer_link.is_valid());
    CHECK(!e->room.is_valid());

    ws.Save();
    e.Save();
  }
  {
    ae::Domain domain{storage};
    auto ws = ChatWorkspace::ptr::Declare(ae::CreateWith{domain}.with_id(ws_id));
    ws.Load();
    CHECK(ws->chats.size() == 1);
    auto e = ws->chats[0];
    CHECK(e.is_valid());
    e.Load();
    CHECK(e->peer_uid == "peer-300");
    CHECK(e->display_name == "Host peer-300");
    CHECK(!e->peer_link.is_valid());
    CHECK(!e->room.is_valid());
  }
}

// Scenario 4: Submitting while unresolved fails and preserves the draft.
void TestScenario4_SubmittingWhileUnresolvedFails() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto ws = CreateWorkspace(domain);
  BindLocalEndpoint(*ws, "endpoint-a");

  auto e = OpenOrSelectChat(*ws, "peer-400", [] {});
  SetDraft(*e, "Draft to Dave");
  CHECK(e->draft == "Draft to Dave");

  auto msg_id = SubmitDraft(*ws, *e, 1000);
  CHECK(msg_id.origin_uid.empty());
  CHECK(msg_id.origin_sequence == 0);
  CHECK(e->draft == "Draft to Dave");
  CHECK(ws->next_message_sequence == 1);
}

// Scenario 5: Bind a locally created ChatRoom and MemoryLink using the command.
void TestScenario5_BindChatRoomAndLink() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto ws = CreateWorkspace(domain);
  auto e = OpenOrSelectChat(*ws, "peer-500", [] {});

  auto link = CreateMemoryLink(domain, ae::ObjId{20}, "peer-500");
  auto room = CreateRoom(domain, ae::ObjId{30});

  bool ok = BindChat(*e, link, room, [] {});
  CHECK(ok);
  CHECK(e->peer_link.id() == link.id());
  CHECK(e->room.id() == room.id());

  // Idempotent binding
  CHECK(BindChat(*e, link, room, [] {}));

  // Conflicting binding rejected
  auto link2 = CreateMemoryLink(domain, ae::ObjId{21}, "peer-other");
  CHECK(!BindChat(*e, link2, room, [] {}));
}

// Scenario 6: Submit a UTF-8 multiline draft; exact text reaches MessageValue.
void TestScenario6_SubmitUtf8MultilineDraft() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto ws = CreateWorkspace(domain);
  BindLocalEndpoint(*ws, "endpoint-a");

  auto e = OpenOrSelectChat(*ws, "peer-600", [] {});
  auto link = CreateMemoryLink(domain, ae::ObjId{20}, "peer-600");
  auto room = CreateRoom(domain, ae::ObjId{30});
  BindChat(*e, link, room, [] {});

  std::string const utf8_multiline =
      "Hello world!\nLine 2 with UTF-8: Привет мир \xF0\x9F\x9A\x80\nLine 3!";
  SetDraft(*e, utf8_multiline);

  auto msg_id = SubmitDraft(*ws, *e, 100000);
  CHECK(msg_id.origin_uid == "endpoint-a");
  CHECK(msg_id.origin_sequence == 1);
  CHECK(e->draft.empty());
  CHECK(room->messages.size() == 1);
  CHECK(room->messages[0].text == utf8_multiline);
  CHECK(room->messages[0].timestamp_us == 100000);
  CHECK(room->messages[0].id == msg_id);
}

// Scenario 7: MessageValue identity/timestamp equal the room's EventRecord metadata.
void TestScenario7_MessageValueIdentityEqualsEventRecord() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto ws = CreateWorkspace(domain);
  BindLocalEndpoint(*ws, "endpoint-a");

  auto e = OpenOrSelectChat(*ws, "peer-700", [] {});
  auto link = CreateMemoryLink(domain, ae::ObjId{20}, "peer-700");
  auto room = CreateRoom(domain, ae::ObjId{30});
  BindChat(*e, link, room, [] {});

  SetDraft(*e, "Message 1");
  auto id1 = SubmitDraft(*ws, *e, 5000);

  CHECK(room->messages.size() == 1);
  CHECK(room->journal.size() == 1);
  auto const& rec = room->journal[0];
  CHECK(rec.identity == id1);
  CHECK(rec.order.timestamp_us == 5000);
  CHECK(room->messages[0].id == rec.identity);
  CHECK(room->messages[0].timestamp_us == rec.order.timestamp_us);
}

// Helper to save all workspace objects in a domain
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

// Scenario 16: Workspace-root Save alone persists nested chat graph reachability.
void TestScenario16_WorkspaceRootSaveReachability() {
  ae::RamDomainStorage storage;
  ae::ObjId const ws_id{10};
  std::string const multiline_msg = "Root-only save\nLine 2";

  {
    ae::Domain domain{storage};
    auto ws = CreateWorkspace(domain, ws_id);
    BindLocalEndpoint(*ws, "endpoint-a");

    auto e = OpenOrSelectChat(*ws, "peer-root", [] {});
    auto link = CreateMemoryLink(domain, ae::ObjId{20}, "peer-root");
    auto room = CreateRoom(domain, ae::ObjId{30});
    BindChat(*e, link, room, [] {});

    SetDraft(*e, multiline_msg);
    SubmitDraft(*ws, *e, 10000);
    SetDraft(*e, "Unsent draft");
    ws.Save();
  }

  {
    ae::Domain domain{storage};
    auto ws = ChatWorkspace::ptr::Declare(ae::CreateWith{domain}.with_id(ws_id));
    ws.Load();
    CHECK(ws);
    CHECK(ws->chats.size() == 1);
    auto entry = ws->chats[0];
    CHECK(entry.is_valid());
    entry.Load();
    CHECK(entry->peer_uid == "peer-root");
    CHECK(entry->draft == "Unsent draft");
    CHECK(entry->peer_link.is_valid());
    entry->peer_link.Load();
    CHECK(entry->peer_link->EndpointUid() == "peer-root");
    CHECK(entry->room.is_valid());
    entry->room.Load();
    CHECK(entry->room->messages.size() == 1);
    CHECK(entry->room->messages[0].text == multiline_msg);
  }
}

// Scenario 8: Save, destroy the whole Domain, reload Workspace:
// chats, selected entry, draft, messages, Link and window bounds restored.
void TestScenario8_SaveDestroyReloadWorkspace() {
  ae::RamDomainStorage storage;
  ae::ObjId const ws_id{10};
  std::string const multiline_msg = "Saved message\nLine 2";

  {
    ae::Domain domain{storage};
    auto ws = CreateWorkspace(domain, ws_id);
    BindLocalEndpoint(*ws, "endpoint-a");

    DesktopBounds bounds{
        .valid = true,
        .x = -150,
        .y = 50,
        .width = 1280,
        .height = 800,
        .maximized = false,
    };
    SetDesktopBounds(*ws, bounds);

    auto e = OpenOrSelectChat(*ws, "peer-800", [] {});
    auto link = CreateMemoryLink(domain, ae::ObjId{20}, "peer-800");
    auto room = CreateRoom(domain, ae::ObjId{30});
    BindChat(*e, link, room, [] {});

    SetDraft(*e, "Draft before submit");
    SubmitDraft(*ws, *e, 10000);

    SetDraft(*e, "Unsent draft restored");

    auto persist = [&]() { SaveWorkspaceGraph(ws); };
    persist();
  }

  {
    ae::Domain domain{storage};
    auto ws = ChatWorkspace::ptr::Declare(ae::CreateWith{domain}.with_id(ws_id));
    ws.Load();

    CHECK(ws->local_endpoint_uid == "endpoint-a");
    CHECK(ws->next_message_sequence == 2);
    CHECK(ws->desktop_bounds.valid);
    CHECK(ws->desktop_bounds.x == -150);
    CHECK(ws->desktop_bounds.y == 50);
    CHECK(ws->desktop_bounds.width == 1280);
    CHECK(ws->desktop_bounds.height == 800);
    CHECK(ws->chats.size() == 1);
    CHECK(ws->selected_chat_id == ws->chats[0].id());

    auto e = ws->chats[0];
    e.Load();
    CHECK(e->peer_uid == "peer-800");
    CHECK(e->display_name == "Host peer-800");
    CHECK(e->draft == "Unsent draft restored");

    CHECK(e->peer_link.is_valid());
    e->peer_link.Load();
    CHECK(e->peer_link->EndpointUid() == "peer-800");

    CHECK(e->room.is_valid());
    e->room.Load();
    CHECK(e->room->messages.size() == 1);
    CHECK(e->room->messages[0].text == "Draft before submit");
    CHECK(e->room->messages[0].id.origin_uid == "endpoint-a");
    CHECK(e->room->messages[0].id.origin_sequence == 1);
  }
}

// Scenario 9: Submit after restart: sequence is greater than the previous reservation.
void TestScenario9_SubmitAfterRestartSequenceAdvances() {
  ae::RamDomainStorage storage;
  ae::ObjId const ws_id{10};

  {
    ae::Domain domain{storage};
    auto ws = CreateWorkspace(domain, ws_id);
    BindLocalEndpoint(*ws, "endpoint-a");

    auto e = OpenOrSelectChat(*ws, "peer-900", [] {});
    auto link = CreateMemoryLink(domain, ae::ObjId{20}, "peer-900");
    auto room = CreateRoom(domain, ae::ObjId{30});
    BindChat(*e, link, room, [] {});

    SetDraft(*e, "Message 1");
    auto id1 = SubmitDraft(*ws, *e, 1000);
    CHECK(id1.origin_sequence == 1);
    CHECK(ws->next_message_sequence == 2);

    SaveWorkspaceGraph(ws);
  }

  {
    ae::Domain domain{storage};
    auto ws = ChatWorkspace::ptr::Declare(ae::CreateWith{domain}.with_id(ws_id));
    ws.Load();
    CHECK(ws->next_message_sequence == 2);

    auto e = ws->chats[0];
    e.Load();
    e->room.Load();

    SetDraft(*e, "Message 2 after restart");
    auto id2 = SubmitDraft(*ws, *e, 2000);
    CHECK(id2.origin_sequence == 2);
    CHECK(id2.origin_sequence > 1);
    CHECK(ws->next_message_sequence == 3);
    CHECK(e->room->messages.size() == 2);
  }
}

// Scenario 10: Two entries have independent drafts and scroll anchors.
void TestScenario10_IndependentDraftsAndScrollAnchors() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto ws = CreateWorkspace(domain);

  auto e1 = OpenOrSelectChat(*ws, "peer-1001", [] {});
  auto e2 = OpenOrSelectChat(*ws, "peer-1002", [] {});

  SetDraft(*e1, "Draft for Judy");
  SetDraft(*e2, "Draft for Kevin");

  ScrollAnchor anchor1{
      .follow_tail = false,
      .first_visible_message = SharedEventId{"endpoint-x", 42},
      .offset_from_message_top = 15.5,
  };
  ScrollAnchor anchor2{
      .follow_tail = true,
      .first_visible_message = {},
      .offset_from_message_top = 0.0,
  };

  SetScroll(*e1, anchor1);
  SetScroll(*e2, anchor2);

  CHECK(e1->draft == "Draft for Judy");
  CHECK(e2->draft == "Draft for Kevin");
  CHECK(e1->scroll == anchor1);
  CHECK(e2->scroll == anchor2);
  CHECK(e1->scroll != e2->scroll);
}

// Scenario 11: Message arrival does not change a non-tail scroll anchor.
void TestScenario11_MessageArrivalDoesNotChangeNonTailScrollAnchor() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto ws = CreateWorkspace(domain);
  BindLocalEndpoint(*ws, "endpoint-a");

  auto e = OpenOrSelectChat(*ws, "peer-1100", [] {});
  auto link = CreateMemoryLink(domain, ae::ObjId{20}, "peer-1100");
  auto room = CreateRoom(domain, ae::ObjId{30});
  BindChat(*e, link, room, [] {});

  ScrollAnchor initial_scroll{
      .follow_tail = false,
      .first_visible_message = SharedEventId{"endpoint-b", 5},
      .offset_from_message_top = 100.0,
  };
  SetScroll(*e, initial_scroll);

  // Submit local draft
  SetDraft(*e, "Local message");
  SubmitDraft(*ws, *e, 1000);

  // Assert scroll anchor is unchanged
  CHECK(e->scroll == initial_scroll);

  // Insert remote message
  MessageValue remote_msg{
      .id = SharedEventId{"endpoint-b", 6},
      .timestamp_us = 1500,
      .text = "Remote reply",
  };
  auto remote_event =
      MessageAddedEvent::ptr::Create(ae::CreateWith{*room->domain});
  remote_event->message = remote_msg;
  room->InsertSharedOrderedEvent(
      remote_event, remote_msg.id,
      SharedEventOrder{.timestamp_us = remote_msg.timestamp_us});

  // Assert scroll anchor is still unchanged
  CHECK(e->scroll == initial_scroll);
}

// Scenario 12: Negative desktop x/y survive persistence.
void TestScenario12_NegativeDesktopBoundsSurvivePersistence() {
  ae::RamDomainStorage storage;
  ae::ObjId const ws_id{10};
  {
    ae::Domain domain{storage};
    auto ws = CreateWorkspace(domain, ws_id);
    DesktopBounds bounds{
        .valid = true,
        .x = -1920,
        .y = -1080,
        .width = 1600,
        .height = 900,
        .maximized = false,
    };
    SetDesktopBounds(*ws, bounds);
    ws.Save();
  }
  {
    ae::Domain domain{storage};
    auto ws = ChatWorkspace::ptr::Declare(ae::CreateWith{domain}.with_id(ws_id));
    ws.Load();
    CHECK(ws->desktop_bounds.valid);
    CHECK(ws->desktop_bounds.x == -1920);
    CHECK(ws->desktop_bounds.y == -1080);
    CHECK(ws->desktop_bounds.width == 1600);
    CHECK(ws->desktop_bounds.height == 900);
    CHECK(!ws->desktop_bounds.maximized);
  }
}

// Scenario 13: Initial creation values exist before InitializeRuntimeNode.
void TestScenario13_InitialCreationValuesExistBeforeInitializeRuntimeNode() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};

  auto entry = ChatEntry::ptr::Create(ae::CreateWith{domain}.with_id(100));
  entry->peer_uid = "peer-admin-preinit";
  entry->display_name = "Preinit Display Name";
  entry->draft = "Preinit draft";
  entry->scroll = ScrollAnchor{.follow_tail = false,
                               .first_visible_message = {"origin", 1},
                               .offset_from_message_top = 42.0};

  CHECK(entry->peer_uid == "peer-admin-preinit");
  CHECK(entry->display_name == "Preinit Display Name");
  CHECK(entry->draft == "Preinit draft");
  CHECK(entry->scroll.offset_from_message_top == 42.0);

  // Initialize runtime node creates base snapshot
  InitializeRuntimeNode(*entry);
  CHECK(entry->base.is_valid());
  CHECK(entry->base.is_loaded());

  // Confirm values in base snapshot match
  auto base_entry = ChatEntry::ptr{entry->base};
  CHECK(base_entry->peer_uid == "peer-admin-preinit");
  CHECK(base_entry->display_name == "Preinit Display Name");
  CHECK(base_entry->draft == "Preinit draft");
  CHECK(base_entry->scroll == entry->scroll);
}

// Scenario 14: Insert message Events at 100, 300, then 200 using explicit different IDs:
// existing journal replay yields 100, 200, 300 in messages, without a
// separate message sort and without changing local draft/scroll state.
void TestScenario14_JournalReplayOrder() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto ws = CreateWorkspace(domain);
  auto e = OpenOrSelectChat(*ws, "peer-1400", [] {});
  auto link = CreateMemoryLink(domain, ae::ObjId{20}, "peer-1400");
  auto room = CreateRoom(domain, ae::ObjId{30});
  BindChat(*e, link, room, [] {});

  SetDraft(*e, "Draft before replay");
  ScrollAnchor anchor{.follow_tail = false,
                      .first_visible_message = {"mid", 1},
                      .offset_from_message_top = 22.0};
  SetScroll(*e, anchor);

  // Insert at 100
  {
    MessageValue msg{.id = {"origin-x", 1}, .timestamp_us = 100, .text = "Msg 100"};
    auto ev = MessageAddedEvent::ptr::Create(ae::CreateWith{domain});
    ev->message = msg;
    room->InsertSharedOrderedEvent(ev, msg.id,
                                   SharedEventOrder{.timestamp_us = 100});
  }

  // Insert at 300
  {
    MessageValue msg{.id = {"origin-y", 2}, .timestamp_us = 300, .text = "Msg 300"};
    auto ev = MessageAddedEvent::ptr::Create(ae::CreateWith{domain});
    ev->message = msg;
    room->InsertSharedOrderedEvent(ev, msg.id,
                                   SharedEventOrder{.timestamp_us = 300});
  }

  CHECK(room->messages.size() == 2);
  CHECK(room->messages[0].timestamp_us == 100);
  CHECK(room->messages[1].timestamp_us == 300);

  // Mid-insert at 200 (forces base replay)
  {
    MessageValue msg{.id = {"origin-z", 3}, .timestamp_us = 200, .text = "Msg 200"};
    auto ev = MessageAddedEvent::ptr::Create(ae::CreateWith{domain});
    ev->message = msg;
    room->InsertSharedOrderedEvent(ev, msg.id,
                                   SharedEventOrder{.timestamp_us = 200});
  }

  CHECK(room->messages.size() == 3);
  CHECK(room->messages[0].timestamp_us == 100);
  CHECK(room->messages[1].timestamp_us == 200);
  CHECK(room->messages[2].timestamp_us == 300);
  CHECK(room->messages[0].text == "Msg 100");
  CHECK(room->messages[1].text == "Msg 200");
  CHECK(room->messages[2].text == "Msg 300");

  // Local entry draft and scroll must remain untouched
  CHECK(e->draft == "Draft before replay");
  CHECK(e->scroll == anchor);
}

// Scenario 15: Export only ChatRoom with BuildNetworkSharedScratch:
// no Workspace or ChatEntry objects are exported;
// no draft, scroll or window state becomes shared state.
void TestScenario15_ExportOnlyChatRoomNetworkShared() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto ws = CreateWorkspace(domain, ae::ObjId{10});
  BindLocalEndpoint(*ws, "endpoint-a");
  SetDesktopBounds(*ws, DesktopBounds{.valid = true, .x = 100, .y = 100});

  auto e = OpenOrSelectChat(*ws, "peer-1500", [] {});
  SetDraft(*e, "Secret local draft");
  SetScroll(*e, ScrollAnchor{.follow_tail = false, .offset_from_message_top = 77.0});

  auto link = CreateMemoryLink(domain, ae::ObjId{20}, "peer-1500");
  auto room = CreateRoom(domain, ae::ObjId{30});
  BindChat(*e, link, room, [] {});

  // Add a message to the room
  SetDraft(*e, "Shared Message 1");
  SubmitDraft(*ws, *e, 1000);

  // Export room to scratch
  ae::RamDomainStorage scratch;
  apptraverse::BuildNetworkSharedScratch(*room, scratch);

  // Verify only the room (and any shared links/shares) are in scratch
  // Specifically, ws and entry IDs must NOT exist in scratch
  auto ws_classes = scratch.Enumerate(ws.id());
  CHECK(ws_classes.empty());

  auto entry_classes = scratch.Enumerate(e.id());
  CHECK(entry_classes.empty());

  auto room_classes = scratch.Enumerate(room.id());
  CHECK(!room_classes.empty());

  // Load from scratch to inspect exported room
  ae::Domain scratch_domain{scratch};
  auto exported_room =
      ChatRoom::ptr::Declare(ae::CreateWith{scratch_domain}.with_id(room.id()));
  exported_room.Load();

  CHECK(exported_room->messages.size() == 1);
  CHECK(exported_room->messages[0].text == "Shared Message 1");
}

// Round-trip with DirectoryDomainStorage in a temporary directory
void TestDirectoryDomainStorageRoundTrip() {
  auto const temp_dir =
      std::filesystem::temp_directory_path() / "apptraverse_chat_demo_test_dir";
  std::filesystem::remove_all(temp_dir);
  std::filesystem::create_directories(temp_dir);

  ae::ObjId const ws_id{10};

  {
    apptraverse::DirectoryDomainStorage storage{temp_dir};
    ae::Domain domain{storage};
    auto ws = CreateWorkspace(domain, ws_id);
    BindLocalEndpoint(*ws, "endpoint-dir-a");

    auto e = OpenOrSelectChat(*ws, "peer-dir", [] {});
    auto link = CreateMemoryLink(domain, ae::ObjId{20}, "peer-dir");
    auto room = CreateRoom(domain, ae::ObjId{30});
    BindChat(*e, link, room, [] {});

    SetDraft(*e, "Directory storage message");
    SubmitDraft(*ws, *e, 12345);

    SetDraft(*e, "Persisted draft in dir");

    SaveWorkspaceGraph(ws);
  }

  {
    apptraverse::DirectoryDomainStorage storage{temp_dir};
    ae::Domain domain{storage};
    auto ws = ChatWorkspace::ptr::Declare(ae::CreateWith{domain}.with_id(ws_id));
    ws.Load();

    CHECK(ws->local_endpoint_uid == "endpoint-dir-a");
    CHECK(ws->chats.size() == 1);

    auto e = ws->chats[0];
    e.Load();
    CHECK(e->peer_uid == "peer-dir");
    CHECK(e->draft == "Persisted draft in dir");

    e->room.Load();
    CHECK(e->room->messages.size() == 1);
    CHECK(e->room->messages[0].text == "Directory storage message");
    CHECK(e->room->messages[0].timestamp_us == 12345);
  }

  // Clean up only our own temporary directory
  std::filesystem::remove_all(temp_dir);
}

void TestSequenceOverflowRejection() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto ws = CreateWorkspace(domain);
  BindLocalEndpoint(*ws, "ep-overflow");

  auto e = OpenOrSelectChat(*ws, "peer-overflow", [] {});
  auto link = CreateMemoryLink(domain, ae::ObjId{20}, "ep-remote");
  auto room = CreateRoom(domain, ae::ObjId{30});
  BindChat(*e, link, room, [] {});

  ws->next_message_sequence = std::numeric_limits<std::uint64_t>::max();
  SetDraft(*e, "overflow draft");

  bool persist_called = false;
  auto id = SubmitDraft(*ws, *e, 1000, [&] { persist_called = true; });

  CHECK(id.origin_uid.empty());
  CHECK(id.origin_sequence == 0);
  CHECK(e->draft == "overflow draft");
  CHECK(room->messages.empty());
  CHECK(ws->next_message_sequence == std::numeric_limits<std::uint64_t>::max());
  CHECK(!persist_called);

  // CanApply checks on MessageSequenceReservedEvent
  auto ev_max =
      MessageSequenceReservedEvent::ptr::Create(ae::CreateWith{domain});
  ev_max->reserved_sequence = std::numeric_limits<std::uint64_t>::max();
  CHECK(!ws->CanApply(*ev_max));

  auto ev_zero =
      MessageSequenceReservedEvent::ptr::Create(ae::CreateWith{domain});
  ev_zero->reserved_sequence = 0;
  CHECK(!ws->CanApply(*ev_zero));

  ws->next_message_sequence = 42;
  auto ev_mismatch =
      MessageSequenceReservedEvent::ptr::Create(ae::CreateWith{domain});
  ev_mismatch->reserved_sequence = 43;
  CHECK(!ws->CanApply(*ev_mismatch));

  auto ev_valid =
      MessageSequenceReservedEvent::ptr::Create(ae::CreateWith{domain});
  ev_valid->reserved_sequence = 42;
  CHECK(ws->CanApply(*ev_valid));
}

void TestSelectChatValidation() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto ws = CreateWorkspace(domain);

  auto e1 = OpenOrSelectChat(*ws, "peer-1", [] {});
  auto e2 = OpenOrSelectChat(*ws, "peer-2", [] {});
  CHECK(ws->selected_chat_id == e2.id());

  bool persist_called = false;

  // 1. Invalid zero ObjId rejected
  CHECK(!SelectChat(*ws, ae::ObjId{}, [&] { persist_called = true; }));
  CHECK(!persist_called);
  CHECK(ws->selected_chat_id == e2.id());

  // 2. Valid Domain object not in chats rejected
  auto outsider = ChatEntry::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*outsider);
  CHECK(!SelectChat(*ws, outsider.id(), [&] { persist_called = true; }));
  CHECK(!persist_called);
  CHECK(ws->selected_chat_id == e2.id());

  // 3. Valid ChatEntry in chats accepted
  CHECK(SelectChat(*ws, e1.id(), [&] { persist_called = true; }));
  CHECK(persist_called);
  CHECK(ws->selected_chat_id == e1.id());

  // CanApply checks
  auto ev_zero = ChatSelectedEvent::ptr::Create(ae::CreateWith{domain});
  ev_zero->entry_id = ae::ObjId{};
  CHECK(!ws->CanApply(*ev_zero));

  auto ev_outsider = ChatSelectedEvent::ptr::Create(ae::CreateWith{domain});
  ev_outsider->entry_id = outsider.id();
  CHECK(!ws->CanApply(*ev_outsider));

  auto ev_valid = ChatSelectedEvent::ptr::Create(ae::CreateWith{domain});
  ev_valid->entry_id = e1.id();
  CHECK(ws->CanApply(*ev_valid));
}

void TestMessageMetadataMatchesJournalMetadata() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto room = CreateRoom(domain);

  SharedEventId const id1{.origin_uid = "origin-a", .origin_sequence = 1};
  SharedEventOrder const order1{.timestamp_us = 1000};

  // 1. Matching metadata succeeds
  {
    auto ev = MessageAddedEvent::ptr::Create(ae::CreateWith{domain});
    ev->message =
        MessageValue{.id = id1, .timestamp_us = 1000, .text = "Hello"};
    CHECK(room->TryInsertShared(ev, id1, order1));
    CHECK(room->messages.size() == 1);
    CHECK(room->journal.size() == 1);
  }

  // 2. Mismatching ID fails
  {
    SharedEventId const id2{.origin_uid = "origin-a", .origin_sequence = 2};
    SharedEventOrder const order2{.timestamp_us = 2000};
    auto ev = MessageAddedEvent::ptr::Create(ae::CreateWith{domain});
    ev->message = MessageValue{
        .id = SharedEventId{.origin_uid = "origin-b", .origin_sequence = 2},
        .timestamp_us = 2000,
        .text = "Mismatch ID"};
    CHECK(!room->TryInsertShared(ev, id2, order2));
    CHECK(room->messages.size() == 1);
    CHECK(room->journal.size() == 1);
  }

  // 3. Mismatching timestamp fails
  {
    SharedEventId const id3{.origin_uid = "origin-a", .origin_sequence = 3};
    SharedEventOrder const order3{.timestamp_us = 3000};
    auto ev = MessageAddedEvent::ptr::Create(ae::CreateWith{domain});
    ev->message = MessageValue{.id = id3,
                               .timestamp_us = 9999,
                               .text = "Mismatch time"};
    CHECK(!room->TryInsertShared(ev, id3, order3));
    CHECK(room->messages.size() == 1);
    CHECK(room->journal.size() == 1);
  }
}

}  // namespace
}  // namespace apptraverse::example::chat_demo

int main() {
  using namespace apptraverse::example::chat_demo;
  std::cout << "Running apptraverse_chat_demo_model_test...\n";

  TestScenario1_OpenTwoAdminIds();
  TestScenario2_OpenFirstIdAgain();
  TestScenario3_EntryPersistWhileUnresolved();
  TestScenario4_SubmittingWhileUnresolvedFails();
  TestScenario5_BindChatRoomAndLink();
  TestScenario6_SubmitUtf8MultilineDraft();
  TestScenario7_MessageValueIdentityEqualsEventRecord();
  TestScenario16_WorkspaceRootSaveReachability();
  TestScenario8_SaveDestroyReloadWorkspace();
  TestScenario9_SubmitAfterRestartSequenceAdvances();
  TestScenario10_IndependentDraftsAndScrollAnchors();
  TestScenario11_MessageArrivalDoesNotChangeNonTailScrollAnchor();
  TestScenario12_NegativeDesktopBoundsSurvivePersistence();
  TestScenario13_InitialCreationValuesExistBeforeInitializeRuntimeNode();
  TestScenario14_JournalReplayOrder();
  TestScenario15_ExportOnlyChatRoomNetworkShared();
  TestDirectoryDomainStorageRoundTrip();
  TestSequenceOverflowRejection();
  TestSelectChatValidation();
  TestMessageMetadataMatchesJournalMetadata();

  std::cout << "All 15 scenarios and DirectoryDomainStorage test passed!\n";
  return 0;
}
