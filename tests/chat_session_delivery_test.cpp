// ChatSession + AetherByteTransport delivery path (FakeAetherFrameEndpoint).
// Counts real sync-frame Send calls separately from bootstrap/control.

#include <chrono>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj_id.h"

#include "aether_link.h"
#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/link.h"
#include "apptraverse/memory_transport.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/shared_network_graph.h"
#include "apptraverse/shared_node.h"
#include "apptraverse/sync_frame.h"
#include "chat_connectivity.h"
#include "chat_model.h"
#include "chat_session.h"
#include "fake_aether_frame_endpoint.h"

#define CHECK(cond)                                                           \
  do {                                                                        \
    if (!(cond)) {                                                            \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"        \
                << __LINE__ << '\n';                                          \
      std::exit(1);                                                           \
    }                                                                         \
  } while (0)

namespace {

using apptraverse::example::chat_demo::ChatEntry;
using apptraverse::example::chat_demo::ChatSession;
using apptraverse::example::chat_demo::ChatSessionConfig;
using apptraverse::example::chat_demo::ChatUiUpdate;
using apptraverse::example::chat_demo::ChatWorkspace;
using apptraverse::example::chat_demo::DemoRole;
using apptraverse::example::chat_demo::IAetherFrameEndpoint;
using apptraverse::example::chat_demo::MessageDeliveryState;
using apptraverse::example::chat_demo::PeerPresence;
using apptraverse::example::chat_demo::SessionLifecycleState;
using apptraverse::example::chat_demo::test::FakeAetherFrameEndpoint;
using apptraverse::example::chat_demo::test::FakeEndpointCoordinator;

struct UiMirror {
  std::unique_ptr<ae::RamDomainStorage> storage =
      std::make_unique<ae::RamDomainStorage>();
  std::unique_ptr<ae::Domain> domain;
  ChatWorkspace::ptr workspace;
};

void ApplyUiUpdate(UiMirror& ui, ChatUiUpdate const& update) {
  if (!update.publication_bytes.has_value() ||
      update.publication_bytes->empty()) {
    return;
  }
  auto const& bytes = *update.publication_bytes;
  apptraverse::ByteSource in;
  in.data = bytes.data();
  in.size = bytes.size();
  if (!ui.workspace.is_valid()) {
    ui.domain = std::make_unique<ae::Domain>(*ui.storage);
    auto root =
        apptraverse::LoadInitialPublication(in, *ui.domain, *ui.storage);
    CHECK(root);
    CHECK(root->GetClassId() == ChatWorkspace::kClassId);
    auto held = ui.domain->Find(root->obj_id);
    CHECK(held);
    ui.workspace = ChatWorkspace::ptr{ui.domain.get(), root->obj_id, {},
                                     std::move(held)};
  } else {
    apptraverse::ApplyStructuralPublicationAndUpdatePresenters(
        in, *ui.domain, *ui.storage, *ui.workspace);
  }
}

ChatEntry::ptr FindEntryByPeerUid(ChatWorkspace& ws,
                                  std::string const& admin) {
  for (auto const& entry : ws.chats) {
    if (entry.is_valid() && entry->peer_uid == admin) {
      return entry;
    }
  }
  return {};
}

inline constexpr ae::ObjId kLocalWorkspaceRootId{10001};
constexpr char const* kUidA = "a1111111-1111-4111-8111-111111111111";
constexpr char const* kUidB = "b2222222-2222-4222-8222-222222222222";

std::filesystem::path MakeTempDir(char const* tag) {
  auto path = std::filesystem::temp_directory_path() /
              (std::string(tag) + "_" +
               std::to_string(
                   std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path);
  return path;
}

void ConsumePublications(ChatSession& session, UiMirror& ui) {
  while (auto update = session.TryTakeUiUpdate()) {
    if (update->publication_bytes.has_value()) {
      ApplyUiUpdate(ui, *update);
    }
  }
}

void WaitReady(ChatSession& session, std::string const& uid) {
  auto const deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (std::chrono::steady_clock::now() < deadline) {
    auto const st = session.GetRuntimeStatus();
    if (st.lifecycle_state == SessionLifecycleState::kReady &&
        st.local_endpoint_uid == uid) {
      return;
    }
    if (st.lifecycle_state == SessionLifecycleState::kFailed) {
      std::cerr << "Ready failed: " << st.error_text << '\n';
      std::exit(1);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  CHECK(false && "timeout waiting Ready");
}

void WaitInitialPublication(ChatSession& session, UiMirror& ui) {
  auto const deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (std::chrono::steady_clock::now() < deadline) {
    ConsumePublications(session, ui);
    if (ui.workspace.is_valid()) {
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  CHECK(false && "timeout waiting initial publication");
}

bool WaitForBoundEntry(ChatSession& session, UiMirror& ui,
                       std::string const& admin) {
  auto const deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(20);
  while (std::chrono::steady_clock::now() < deadline) {
    ConsumePublications(session, ui);
    auto entry = FindEntryByPeerUid(*ui.workspace, admin);
    if (entry.is_valid() && entry->room.is_valid() &&
        entry->peer_link.is_valid()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return false;
}

std::size_t MessageCount(UiMirror& ui, std::string const& admin) {
  auto entry = FindEntryByPeerUid(*ui.workspace, admin);
  if (!entry.is_valid() || !entry->room.is_valid()) {
    return 0;
  }
  if (!entry->room.is_loaded()) {
    entry->room.Load();
  }
  return entry->room->messages.size();
}

struct PeerSession {
  std::filesystem::path state_dir;
  std::shared_ptr<FakeAetherFrameEndpoint*> fake_slot =
      std::make_shared<FakeAetherFrameEndpoint*>(nullptr);
  std::unique_ptr<ChatSession> session;

  FakeAetherFrameEndpoint* fake() const { return *fake_slot; }
};

PeerSession MakePeer(FakeEndpointCoordinator& coordinator,
                     std::string const& uid) {
  PeerSession peer;
  peer.state_dir = MakeTempDir("chat_deliv");
  auto fake_slot = peer.fake_slot;
  peer.session = std::make_unique<ChatSession>(
      [&coordinator, fake_slot, uid]()
          -> std::unique_ptr<IAetherFrameEndpoint> {
        auto fake =
            std::make_unique<FakeAetherFrameEndpoint>(coordinator, uid);
        *fake_slot = fake.get();
        return fake;
      });
  return peer;
}

void StopSessionKeepState(PeerSession& peer) {
  if (peer.session) {
    peer.session->RequestStop();
    peer.session->Join();
    peer.session.reset();
  }
  peer.fake_slot = std::make_shared<FakeAetherFrameEndpoint*>(nullptr);
}

void DestroyPeerState(PeerSession& peer) {
  StopSessionKeepState(peer);
  std::filesystem::remove_all(peer.state_dir);
}

FakeAetherFrameEndpoint* WaitFake(PeerSession& peer) {
  auto const deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(8);
  while (std::chrono::steady_clock::now() < deadline) {
    if (peer.fake() != nullptr) {
      return peer.fake();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  CHECK(false && "fake endpoint was not created");
  return nullptr;
}

void BringOnline(FakeAetherFrameEndpoint* a, FakeAetherFrameEndpoint* b) {
  a->InjectPresence(b->local_uid(), PeerPresence::kOnline);
  b->InjectPresence(a->local_uid(), PeerPresence::kOnline);
}

void BringOffline(FakeAetherFrameEndpoint* a, FakeAetherFrameEndpoint* b) {
  a->InjectPresence(b->local_uid(), PeerPresence::kOffline);
  b->InjectPresence(a->local_uid(), PeerPresence::kOffline);
}

struct BoundPair {
  PeerSession a;
  PeerSession b;
  UiMirror ui_a;
  UiMirror ui_b;
};

BoundPair BootstrapBound(FakeEndpointCoordinator& coordinator) {
  BoundPair pair{
      .a = MakePeer(coordinator, kUidA),
      .b = MakePeer(coordinator, kUidB),
  };
  CHECK(pair.a.session->Start(
      ChatSessionConfig{.state_dir = pair.a.state_dir, .role = DemoRole::kHost},
      [] {}));
  CHECK(pair.b.session->Start(
      ChatSessionConfig{.state_dir = pair.b.state_dir, .role = DemoRole::kClient},
      [] {}));
  WaitInitialPublication(*pair.a.session, pair.ui_a);
  WaitInitialPublication(*pair.b.session, pair.ui_b);
  WaitReady(*pair.a.session, kUidA);
  WaitReady(*pair.b.session, kUidB);
  WaitFake(pair.a);
  WaitFake(pair.b);

  pair.b.session->SetHostUidInput(std::string{kUidA});
  pair.b.session->JoinHost();
  BringOnline(pair.a.fake(), pair.b.fake());
  CHECK(WaitForBoundEntry(*pair.a.session, pair.ui_a, kUidB));
  CHECK(WaitForBoundEntry(*pair.b.session, pair.ui_b, kUidA));
  return pair;
}

bool WaitDelivery(ChatSession& session, SharedEventId const& id,
                  MessageDeliveryState want,
                  std::chrono::milliseconds timeout = std::chrono::seconds(15)) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    auto const st = session.GetRuntimeStatus();
    auto it = st.delivery_by_event_id.find(id);
    if (it != st.delivery_by_event_id.end() && it->second == want) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
  }
  return false;
}

SharedEventId FirstOwnMessageId(UiMirror& ui, std::string const& peer_admin,
                                std::string const& local_uid) {
  auto entry = FindEntryByPeerUid(*ui.workspace, peer_admin);
  CHECK(entry.is_valid() && entry->room.is_valid());
  if (!entry->room.is_loaded()) {
    entry->room.Load();
  }
  for (auto const& message : entry->room->messages) {
    if (message.id.origin_uid == local_uid && message.id.origin_sequence != 0) {
      return message.id;
    }
  }
  return {};
}

void CheckpointAndWait(ChatSession& session, std::uint64_t id) {
  CHECK(session.Checkpoint(id));
  auto const deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (std::chrono::steady_clock::now() < deadline) {
    if (session.GetRuntimeStatus().completed_checkpoint_id >= id) {
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  CHECK(false && "checkpoint timeout");
}

// Defect (pre-fix): ChatSession SyncRetryState sent initial while Offline.
void TestOfflineSuppressesSyncSendsAndResumes() {
  std::cout << "  offline-suppress: begin\n";
  apptraverse::MemoryNetwork network;
  FakeEndpointCoordinator coordinator{network};
  coordinator.Start();
  auto pair = BootstrapBound(coordinator);

  // Let initial sync settle.
  auto const settle =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < settle) {
    ConsumePublications(*pair.a.session, pair.ui_a);
    ConsumePublications(*pair.b.session, pair.ui_b);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
  }

  BringOffline(pair.a.fake(), pair.b.fake());
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  coordinator.ResetSyncSendCounts();

  auto entry_a = FindEntryByPeerUid(*pair.ui_a.workspace, kUidB);
  CHECK(entry_a.is_valid());
  pair.a.session->EditDraft(entry_a.id(), "while-offline", 1);
  pair.a.session->SendDraft(entry_a.id(), "while-offline", 1);

  // Offline must not emit sync Event/NodeState frames.
  auto const offline_watch =
      std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (std::chrono::steady_clock::now() < offline_watch) {
    ConsumePublications(*pair.a.session, pair.ui_a);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  auto const offline_sends = coordinator.SyncSends(kUidA, kUidB);
  CHECK(offline_sends.event == 0);
  CHECK(offline_sends.node_state == 0);

  // Offline → Online resumes without restart.
  BringOnline(pair.a.fake(), pair.b.fake());
  auto const resume =
      std::chrono::steady_clock::now() + std::chrono::seconds(15);
  while (std::chrono::steady_clock::now() < resume) {
    ConsumePublications(*pair.a.session, pair.ui_a);
    ConsumePublications(*pair.b.session, pair.ui_b);
    if (MessageCount(pair.ui_b, kUidA) >= 1) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
  }
  CHECK(MessageCount(pair.ui_b, kUidA) >= 1);
  auto const mid = FirstOwnMessageId(pair.ui_a, kUidB, kUidA);
  CHECK(mid.origin_sequence != 0);
  CHECK(WaitDelivery(*pair.a.session, mid, MessageDeliveryState::kDelivered));

  DestroyPeerState(pair.a);
  DestroyPeerState(pair.b);
  coordinator.RequestStop();
  coordinator.Join();
  std::cout << "  offline-suppress: ok\n";
}

void TestRepeatedOnlineDoesNotExtraSend() {
  std::cout << "  repeated-online: begin\n";
  apptraverse::MemoryNetwork network;
  FakeEndpointCoordinator coordinator{network};
  coordinator.Start();
  auto pair = BootstrapBound(coordinator);

  auto entry_a = FindEntryByPeerUid(*pair.ui_a.workspace, kUidB);
  pair.a.session->EditDraft(entry_a.id(), "settle-msg", 1);
  pair.a.session->SendDraft(entry_a.id(), "settle-msg", 1);
  auto const settle =
      std::chrono::steady_clock::now() + std::chrono::seconds(15);
  while (std::chrono::steady_clock::now() < settle) {
    ConsumePublications(*pair.a.session, pair.ui_a);
    ConsumePublications(*pair.b.session, pair.ui_b);
    auto mid = FirstOwnMessageId(pair.ui_a, kUidB, kUidA);
    if (mid.origin_sequence != 0 &&
        WaitDelivery(*pair.a.session, mid, MessageDeliveryState::kDelivered,
                     std::chrono::milliseconds(50))) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
  }
  auto mid = FirstOwnMessageId(pair.ui_a, kUidB, kUidA);
  CHECK(WaitDelivery(*pair.a.session, mid, MessageDeliveryState::kDelivered));

  coordinator.ResetSyncSendCounts();
  for (int i = 0; i < 5; ++i) {
    pair.a.fake()->InjectPresence(kUidB, PeerPresence::kOnline);
    pair.b.fake()->InjectPresence(kUidA, PeerPresence::kOnline);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  std::this_thread::sleep_for(std::chrono::seconds(2));
  auto const after = coordinator.SyncSends(kUidA, kUidB);
  CHECK(after.event == 0);
  CHECK(after.node_state == 0);

  DestroyPeerState(pair.a);
  DestroyPeerState(pair.b);
  coordinator.RequestStop();
  coordinator.Join();
  std::cout << "  repeated-online: ok\n";
}

void TestLostAckRetriesThenDelivered() {
  std::cout << "  lost-ack: begin\n";
  apptraverse::MemoryNetwork network;
  FakeEndpointCoordinator coordinator{network};
  coordinator.Start();
  auto pair = BootstrapBound(coordinator);

  // Drop first few Event ACKs from B→A so A's pending stays unacked.
  coordinator.DropNextDataTo(kUidA, 3);

  auto entry_a = FindEntryByPeerUid(*pair.ui_a.workspace, kUidB);
  pair.a.session->EditDraft(entry_a.id(), "need-ack", 1);
  pair.a.session->SendDraft(entry_a.id(), "need-ack", 1);

  auto const mid_deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(10);
  SharedEventId mid{};
  while (std::chrono::steady_clock::now() < mid_deadline) {
    ConsumePublications(*pair.a.session, pair.ui_a);
    ConsumePublications(*pair.b.session, pair.ui_b);
    mid = FirstOwnMessageId(pair.ui_a, kUidB, kUidA);
    if (mid.origin_sequence != 0) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
  }
  CHECK(mid.origin_sequence != 0);

  // While ACKs are lost, must not claim Delivered.
  std::this_thread::sleep_for(std::chrono::seconds(1));
  {
    auto const st = pair.a.session->GetRuntimeStatus();
    auto it = st.delivery_by_event_id.find(mid);
    if (it != st.delivery_by_event_id.end()) {
      CHECK(it->second != MessageDeliveryState::kDelivered);
    }
  }

  // After drops are exhausted, retries + ACK → Delivered.
  CHECK(WaitDelivery(*pair.a.session, mid, MessageDeliveryState::kDelivered,
                     std::chrono::seconds(20)));
  CHECK(MessageCount(pair.ui_b, kUidA) >= 1);

  DestroyPeerState(pair.a);
  DestroyPeerState(pair.b);
  coordinator.RequestStop();
  coordinator.Join();
  std::cout << "  lost-ack: ok\n";
}

void TestRestoreUnackedThenResume() {
  std::cout << "  restore-unacked: begin\n";
  apptraverse::MemoryNetwork network;
  FakeEndpointCoordinator coordinator{network};
  coordinator.Start();
  auto pair = BootstrapBound(coordinator);

  // Prevent ACK so message stays pending, then checkpoint and restart.
  coordinator.DropNextDataTo(kUidA, 50);
  auto entry_a = FindEntryByPeerUid(*pair.ui_a.workspace, kUidB);
  pair.a.session->EditDraft(entry_a.id(), "persist-pending", 1);
  pair.a.session->SendDraft(entry_a.id(), "persist-pending", 1);

  auto const mid_deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(10);
  SharedEventId mid{};
  while (std::chrono::steady_clock::now() < mid_deadline) {
    ConsumePublications(*pair.a.session, pair.ui_a);
    mid = FirstOwnMessageId(pair.ui_a, kUidB, kUidA);
    if (mid.origin_sequence != 0) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
  }
  CHECK(mid.origin_sequence != 0);
  CheckpointAndWait(*pair.a.session, 1);
  CheckpointAndWait(*pair.b.session, 1);

  auto const state_a = pair.a.state_dir;
  auto const state_b = pair.b.state_dir;
  StopSessionKeepState(pair.a);
  StopSessionKeepState(pair.b);

  // Fresh coordinator without the drop budget.
  coordinator.RequestStop();
  coordinator.Join();
  apptraverse::MemoryNetwork network2;
  FakeEndpointCoordinator coordinator2{network2};
  coordinator2.Start();

  pair.a = MakePeer(coordinator2, kUidA);
  pair.a.state_dir = state_a;
  pair.b = MakePeer(coordinator2, kUidB);
  pair.b.state_dir = state_b;
  pair.ui_a = {};
  pair.ui_b = {};
  auto fake_slot_a = pair.a.fake_slot;
  auto fake_slot_b = pair.b.fake_slot;
  pair.a.session = std::make_unique<ChatSession>(
      [&coordinator2, fake_slot_a]()
          -> std::unique_ptr<IAetherFrameEndpoint> {
        auto fake =
            std::make_unique<FakeAetherFrameEndpoint>(coordinator2, kUidA);
        *fake_slot_a = fake.get();
        return fake;
      });
  pair.b.session = std::make_unique<ChatSession>(
      [&coordinator2, fake_slot_b]()
          -> std::unique_ptr<IAetherFrameEndpoint> {
        auto fake =
            std::make_unique<FakeAetherFrameEndpoint>(coordinator2, kUidB);
        *fake_slot_b = fake.get();
        return fake;
      });
  CHECK(pair.a.session->Start(
      ChatSessionConfig{.state_dir = state_a, .role = DemoRole::kHost},
      [] {}));
  CHECK(pair.b.session->Start(
      ChatSessionConfig{.state_dir = state_b, .role = DemoRole::kClient},
      [] {}));
  WaitInitialPublication(*pair.a.session, pair.ui_a);
  WaitInitialPublication(*pair.b.session, pair.ui_b);
  WaitReady(*pair.a.session, kUidA);
  WaitReady(*pair.b.session, kUidB);
  WaitFake(pair.a);
  WaitFake(pair.b);
  BringOnline(pair.a.fake(), pair.b.fake());

  auto const resume =
      std::chrono::steady_clock::now() + std::chrono::seconds(20);
  while (std::chrono::steady_clock::now() < resume) {
    ConsumePublications(*pair.a.session, pair.ui_a);
    ConsumePublications(*pair.b.session, pair.ui_b);
    if (MessageCount(pair.ui_b, kUidA) >= 1 &&
        WaitDelivery(*pair.a.session, mid, MessageDeliveryState::kDelivered,
                     std::chrono::milliseconds(50))) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
  }
  CHECK(MessageCount(pair.ui_b, kUidA) >= 1);
  CHECK(WaitDelivery(*pair.a.session, mid, MessageDeliveryState::kDelivered));

  DestroyPeerState(pair.a);
  DestroyPeerState(pair.b);
  coordinator2.RequestStop();
  coordinator2.Join();
  std::cout << "  restore-unacked: ok\n";
}

void TestRefuseIncompatibleRestoredDialogLeavesStorage() {
  std::cout << "  refuse-incompatible: begin\n";
  apptraverse::MemoryNetwork network;
  FakeEndpointCoordinator coordinator{network};
  coordinator.Start();
  auto pair = BootstrapBound(coordinator);
  CheckpointAndWait(*pair.a.session, 1);
  auto const state_a = pair.a.state_dir;
  StopSessionKeepState(pair.a);
  DestroyPeerState(pair.b);
  coordinator.RequestStop();
  coordinator.Join();

  // Corrupt the persisted room: force a third share entry without Events.
  {
    apptraverse::DirectoryDomainStorage storage{state_a / "model"};
    ae::Domain domain{storage};
    auto workspace = ChatWorkspace::ptr::Declare(
        ae::CreateWith{domain}.with_id(kLocalWorkspaceRootId));
    workspace.Load();
    CHECK(workspace.is_valid());
    CHECK(!workspace->chats.empty());
    auto entry = workspace->chats[0];
    CHECK(entry.is_valid() && entry->room.is_valid());
    if (!entry->room.is_loaded()) {
      entry->room.Load();
    }
    CHECK(entry->room->shares.size() == 2);
    auto rogue = apptraverse::MemoryLink::ptr::Create(ae::CreateWith{domain});
    rogue->endpoint_uid = "c3333333-3333-4333-8333-333333333333";
    apptraverse::InitializeRuntimeNode(*rogue);
    rogue.Save();
    entry->room->shares.push_back(apptraverse::Share{
        .share_id = ae::ObjId{999001},
        .link = rogue,
        .access = static_cast<std::uint8_t>(
            apptraverse::ShareAccess::ReadWrite),
    });
    entry->room.Save();
    workspace.Save();
  }

  // Snapshot storage contents for unchanged check.
  auto SnapshotFiles = [](std::filesystem::path const& root) {
    std::map<std::string, std::uintmax_t> out;
    for (auto const& entry :
         std::filesystem::recursive_directory_iterator(root)) {
      if (entry.is_regular_file()) {
        out[entry.path().lexically_relative(root).string()] =
            entry.file_size();
      }
    }
    return out;
  };
  auto const before = SnapshotFiles(state_a / "model");

  apptraverse::MemoryNetwork network2;
  FakeEndpointCoordinator coord2{network2};
  coord2.Start();
  PeerSession restarted = MakePeer(coord2, kUidA);
  restarted.state_dir = state_a;
  auto fake_slot = restarted.fake_slot;
  restarted.session = std::make_unique<ChatSession>(
      [&coord2, fake_slot]() -> std::unique_ptr<IAetherFrameEndpoint> {
        auto fake =
            std::make_unique<FakeAetherFrameEndpoint>(coord2, kUidA);
        *fake_slot = fake.get();
        return fake;
      });
  CHECK(restarted.session->Start(
      ChatSessionConfig{.state_dir = state_a, .role = DemoRole::kHost},
      [] {}));

  auto const deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(15);
  bool failed = false;
  std::string err;
  while (std::chrono::steady_clock::now() < deadline) {
    auto const st = restarted.session->GetRuntimeStatus();
    if (st.lifecycle_state == SessionLifecycleState::kFailed) {
      failed = true;
      err = st.error_text;
      break;
    }
    if (restarted.session->IsFinished()) {
      failed = st.lifecycle_state == SessionLifecycleState::kFailed;
      err = st.error_text;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
  }
  CHECK(failed);
  CHECK(err.find("Incompatible stored dialog") != std::string::npos);

  restarted.session->RequestStop();
  restarted.session->Join();
  auto const after = SnapshotFiles(state_a / "model");
  CHECK(before == after);

  std::filesystem::remove_all(state_a);
  coord2.RequestStop();
  coord2.Join();
  std::cout << "  refuse-incompatible: ok\n";
}

void TestUnknownAllowsInitialAttempt() {
  std::cout << "  unknown-attempt: begin\n";
  apptraverse::MemoryNetwork network;
  FakeEndpointCoordinator coordinator{network};
  coordinator.Start();

  auto peer_a = MakePeer(coordinator, kUidA);
  auto peer_b = MakePeer(coordinator, kUidB);
  UiMirror ui_a;
  UiMirror ui_b;
  CHECK(peer_a.session->Start(
      ChatSessionConfig{.state_dir = peer_a.state_dir, .role = DemoRole::kHost},
      [] {}));
  CHECK(peer_b.session->Start(
      ChatSessionConfig{.state_dir = peer_b.state_dir, .role = DemoRole::kClient},
      [] {}));
  WaitInitialPublication(*peer_a.session, ui_a);
  WaitInitialPublication(*peer_b.session, ui_b);
  WaitReady(*peer_a.session, kUidA);
  WaitReady(*peer_b.session, kUidB);
  WaitFake(peer_a);
  WaitFake(peer_b);

  // Leave presence at Unknown (no InjectPresence Online).
  peer_b.session->SetHostUidInput(std::string{kUidA});
  peer_b.session->JoinHost();

  auto const deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(15);
  bool saw_node_state = false;
  while (std::chrono::steady_clock::now() < deadline) {
    ConsumePublications(*peer_a.session, ui_a);
    ConsumePublications(*peer_b.session, ui_b);
    if (coordinator.SyncSends(kUidA, kUidB).node_state > 0) {
      saw_node_state = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
  }
  CHECK(saw_node_state);

  // Complete with Online so teardown is clean.
  BringOnline(peer_a.fake(), peer_b.fake());
  CHECK(WaitForBoundEntry(*peer_a.session, ui_a, kUidB));
  CHECK(WaitForBoundEntry(*peer_b.session, ui_b, kUidA));

  DestroyPeerState(peer_a);
  DestroyPeerState(peer_b);
  coordinator.RequestStop();
  coordinator.Join();
  std::cout << "  unknown-attempt: ok\n";
}

}  // namespace

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::example::chat_demo::EnsureChatDemoModelRegistration();
  apptraverse::example::chat_demo::EnsureAetherLinkRegistration();

  std::cout << std::unitbuf;
  std::cout << "Running chat_session_delivery_test...\n";
  TestOfflineSuppressesSyncSendsAndResumes();
  TestRepeatedOnlineDoesNotExtraSend();
  TestLostAckRetriesThenDelivered();
  TestRestoreUnackedThenResume();
  TestRefuseIncompatibleRestoredDialogLeavesStorage();
  TestUnknownAllowsInitialAttempt();
  std::cout << "chat_session_delivery_test passed!\n";
  return 0;
}
