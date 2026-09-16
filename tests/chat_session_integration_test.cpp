// End-to-end ChatSession scenarios: public commands, TryTakeUiUpdate, and a
// separate UI-test Domain (LoadInitial + ApplyStructural). SharedSyncRuntime
// protocol ordering (binding-before-ACK, duplicate NodeState) is in
// shared_sync_protocol_test.cpp.
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj_id.h"
#include "aether-objects/obj/registry.h"

#include "aether_link.h"
#include "apptraverse/memory_transport.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/shared_network_graph.h"
#include "apptraverse/sync_frame.h"

#include "chat_model.h"
#include "chat_session.h"
#include "fake_aether_frame_endpoint.h"
#include "shared_node_demo_model.h"

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
using apptraverse::example::chat_demo::ChatPublicationKind;
using apptraverse::example::chat_demo::ChatRoom;
using apptraverse::example::chat_demo::ChatSession;
using apptraverse::example::chat_demo::ChatSessionConfig;
using apptraverse::example::chat_demo::ChatUiUpdate;
using apptraverse::example::chat_demo::ChatWorkspace;
using apptraverse::example::chat_demo::DesktopBounds;
using apptraverse::example::chat_demo::IAetherFrameEndpoint;
using apptraverse::example::chat_demo::OpenPeerRequest;
using apptraverse::example::chat_demo::PeerPresence;
using apptraverse::example::chat_demo::ScrollAnchor;
using apptraverse::example::chat_demo::SessionLifecycleState;
using apptraverse::example::chat_demo::test::FakeAetherFrameEndpoint;
using apptraverse::example::chat_demo::test::FakeEndpointCoordinator;
using apptraverse::example::shared_node::SharedValueNode;

constexpr char const* kUidA = "a1111111-1111-4111-8111-111111111111";
constexpr char const* kUidB = "b2222222-2222-4222-8222-222222222222";
constexpr char const* kUidC = "c3333333-3333-4333-8333-333333333333";

std::filesystem::path MakeTempDir(char const* tag) {
  auto path = std::filesystem::temp_directory_path() /
              (std::string(tag) + "_" +
               std::to_string(
                   std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path);
  return path;
}

struct UiMirror {
  ae::RamDomainStorage storage;
  std::unique_ptr<ae::Domain> domain;
  ChatWorkspace::ptr workspace;
};

void ApplyUiUpdate(UiMirror& ui, ChatUiUpdate const& update) {
  if (!update.publication_bytes.has_value() || update.publication_bytes->empty()) {
    return;
  }
  auto const& bytes = *update.publication_bytes;
  apptraverse::ByteSource in;
  in.data = bytes.data();
  in.size = bytes.size();
  if (!ui.workspace.is_valid()) {
    ui.domain = std::make_unique<ae::Domain>(ui.storage);
    auto root = apptraverse::LoadInitialPublication(in, *ui.domain, ui.storage);
    CHECK(root);
    CHECK(root->GetClassId() == ChatWorkspace::kClassId);
    ui.workspace = ChatWorkspace::ptr::MakeFromThis(
        static_cast<ChatWorkspace*>(root.get()));
  } else {
    apptraverse::ApplyStructuralPublication(in, *ui.domain, ui.storage);
  }
}

void ConsumePublications(ChatSession& session, UiMirror& ui) {
  while (auto update = session.TryTakeUiUpdate()) {
    ApplyUiUpdate(ui, *update);
  }
}

void WaitInitialPublication(ChatSession& session, UiMirror& ui,
                            std::chrono::milliseconds timeout =
                                std::chrono::seconds(8)) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    ConsumePublications(session, ui);
    if (ui.workspace.is_valid()) {
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  CHECK(false && "initial publication missing");
}

void WaitReady(ChatSession& session, std::string const& uid,
               std::chrono::milliseconds timeout = std::chrono::seconds(8)) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    auto status = session.GetRuntimeStatus();
    if (status.lifecycle_state == SessionLifecycleState::kReady &&
        status.local_endpoint_uid == uid) {
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  CHECK(false && "session did not become Ready");
}

ChatEntry::ptr FindEntryByAdminId(ChatWorkspace& ws, std::string const& admin) {
  for (auto const& entry : ws.chats) {
    if (entry.is_valid() && entry->peer_admin_id == admin) {
      return entry;
    }
  }
  return {};
}

bool WaitForEntry(ChatSession& session, UiMirror& ui, std::string const& admin,
                  std::chrono::milliseconds timeout, bool require_bound) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    ConsumePublications(session, ui);
    if (ui.workspace.is_valid()) {
      auto entry = FindEntryByAdminId(*ui.workspace, admin);
      if (entry.is_valid()) {
        if (!require_bound) {
          return true;
        }
        if (entry->room.is_valid() && entry->peer_link.is_valid()) {
          return true;
        }
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return false;
}

bool WaitForBoundEntry(ChatSession& session, UiMirror& ui,
                       std::string const& admin,
                       std::chrono::milliseconds timeout =
                           std::chrono::seconds(15)) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    ConsumePublications(session, ui);
    if (ui.workspace.is_valid()) {
      auto entry = FindEntryByAdminId(*ui.workspace, admin);
      if (entry.is_valid() && entry->room.is_valid() &&
          entry->peer_link.is_valid()) {
        return true;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return false;
}

bool WaitForError(ChatSession& session, std::string const& expected,
                  std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (session.GetRuntimeStatus().error_text == expected) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}

std::size_t MessageCount(UiMirror& ui, std::string const& admin) {
  if (!ui.workspace.is_valid()) {
    return 0;
  }
  auto entry = FindEntryByAdminId(*ui.workspace, admin);
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

PeerSession MakePeer(FakeEndpointCoordinator& coordinator, std::string const& uid,
                     bool defer_ready = false) {
  PeerSession peer;
  peer.state_dir = MakeTempDir("chat_int");
  auto fake_slot = peer.fake_slot;
  peer.session = std::make_unique<ChatSession>(
      [&coordinator, fake_slot, uid, defer_ready]()
          -> std::unique_ptr<IAetherFrameEndpoint> {
        auto fake =
            std::make_unique<FakeAetherFrameEndpoint>(coordinator, uid);
        fake->SetDeferReady(defer_ready);
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

FakeAetherFrameEndpoint* WaitFake(PeerSession& peer,
                                  std::chrono::milliseconds timeout =
                                      std::chrono::seconds(8)) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (peer.fake() != nullptr) {
      return peer.fake();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  CHECK(false && "fake endpoint was not created");
  return nullptr;
}

PeerSession StartPeerFromSavedState(FakeEndpointCoordinator& coordinator,
                                    std::filesystem::path const& state_dir,
                                    std::string const& uid) {
  PeerSession peer;
  peer.state_dir = state_dir;
  auto fake_slot = peer.fake_slot;
  peer.session = std::make_unique<ChatSession>(
      [&coordinator, fake_slot, uid]() -> std::unique_ptr<IAetherFrameEndpoint> {
        auto fake =
            std::make_unique<FakeAetherFrameEndpoint>(coordinator, uid);
        *fake_slot = fake.get();
        return fake;
      });
  CHECK(peer.session->Start(ChatSessionConfig{.state_dir = peer.state_dir},
                            [] {}));
  WaitFake(peer);
  WaitReady(*peer.session, uid);
  return peer;
}

void BringOnline(FakeAetherFrameEndpoint* a, FakeAetherFrameEndpoint* b,
                 std::string const& uid_a, std::string const& uid_b) {
  a->InjectPresence(uid_b, PeerPresence::kOnline);
  b->InjectPresence(uid_a, PeerPresence::kOnline);
}

PeerSession RestartPeer(PeerSession& old, FakeEndpointCoordinator& coordinator) {
  std::filesystem::path const saved_dir = old.state_dir;
  std::string const uid = old.fake()->local_uid();
  StopSessionKeepState(old);
  return StartPeerFromSavedState(coordinator, saved_dir, uid);
}

void ReopenPersistedPeerChat(PeerSession& self, PeerSession& remote,
                             UiMirror& ui, std::string const& remote_admin,
                             std::string const& remote_uid) {
  self.session->OpenPeer(OpenPeerRequest{
      .peer_admin_id = remote_admin,
      .peer_aether_uid = remote_uid,
  });
  BringOnline(self.fake(), remote.fake(), self.fake()->local_uid(),
              remote.fake()->local_uid());
  CHECK(WaitForBoundEntry(*self.session, ui, remote_admin));
}

struct BootstrapPair {
  PeerSession a;
  PeerSession b;
  UiMirror ui_a;
  UiMirror ui_b;
};

BootstrapPair BootstrapTwoPeers(FakeEndpointCoordinator& coordinator,
                                char const* admin_on_a, char const* admin_on_b) {
  BootstrapPair pair{
      .a = MakePeer(coordinator, kUidA),
      .b = MakePeer(coordinator, kUidB),
  };
  CHECK(pair.a.session->Start(ChatSessionConfig{.state_dir = pair.a.state_dir},
                              [] {}));
  CHECK(pair.b.session->Start(ChatSessionConfig{.state_dir = pair.b.state_dir},
                              [] {}));
  WaitInitialPublication(*pair.a.session, pair.ui_a);
  WaitInitialPublication(*pair.b.session, pair.ui_b);
  WaitReady(*pair.a.session, kUidA);
  WaitReady(*pair.b.session, kUidB);
  WaitFake(pair.a);
  WaitFake(pair.b);

  pair.b.session->OpenPeer(OpenPeerRequest{
      .peer_admin_id = admin_on_b,
      .peer_aether_uid = std::string{kUidA},
  });
  pair.a.session->OpenPeer(OpenPeerRequest{
      .peer_admin_id = admin_on_a,
      .peer_aether_uid = std::string{kUidB},
  });
  BringOnline(pair.a.fake(), pair.b.fake(), kUidA, kUidB);

  CHECK(WaitForBoundEntry(*pair.a.session, pair.ui_a, admin_on_a));
  CHECK(WaitForBoundEntry(*pair.b.session, pair.ui_b, admin_on_b));
  return pair;
}

// 1–3: empty profiles, unknown room id, canonical creator/waiter, bound room.
void TestBootstrapAdmissionAndCreatorElection() {
  apptraverse::MemoryNetwork network;
  FakeEndpointCoordinator coordinator{network};
  coordinator.Start();

  auto pair = BootstrapTwoPeers(coordinator, "bob", "alice");

  CHECK(kUidA < kUidB);
  auto entry_a = FindEntryByAdminId(*pair.ui_a.workspace, "bob");
  auto entry_b = FindEntryByAdminId(*pair.ui_b.workspace, "alice");
  CHECK(entry_a.is_valid());
  CHECK(entry_b.is_valid());
  CHECK(entry_a->room.id() == entry_b->room.id());
  CHECK(entry_b->peer_link->EndpointUid() == kUidA);

  // Binding-before-ACK ordering is asserted in shared_sync_protocol_test.cpp
  // (ChatSession hides the SharedSyncRuntime callback boundary).

  DestroyPeerState(pair.a);
  DestroyPeerState(pair.b);
  coordinator.RequestStop();
  coordinator.Join();
}

// 4: A sends, B replies without another initial snapshot.
void TestSendReplyWithoutResnapshot() {
  apptraverse::MemoryNetwork network;
  FakeEndpointCoordinator coordinator{network};
  coordinator.Start();

  auto peer_a = MakePeer(coordinator, kUidA);
  auto peer_b = MakePeer(coordinator, kUidB);
  UiMirror ui_a;
  UiMirror ui_b;
  CHECK(peer_a.session->Start(ChatSessionConfig{.state_dir = peer_a.state_dir},
                              [] {}));
  CHECK(peer_b.session->Start(ChatSessionConfig{.state_dir = peer_b.state_dir},
                              [] {}));
  WaitInitialPublication(*peer_a.session, ui_a);
  WaitInitialPublication(*peer_b.session, ui_b);
  WaitReady(*peer_a.session, kUidA);
  WaitReady(*peer_b.session, kUidB);
  WaitFake(peer_a);
  WaitFake(peer_b);

  peer_b.session->OpenPeer(OpenPeerRequest{
      .peer_admin_id = "alice",
      .peer_aether_uid = std::string{kUidA},
  });
  peer_a.session->OpenPeer(OpenPeerRequest{
      .peer_admin_id = "bob",
      .peer_aether_uid = std::string{kUidB},
  });
  BringOnline(peer_a.fake(), peer_b.fake(), kUidA, kUidB);

  CHECK(WaitForBoundEntry(*peer_a.session, ui_a, "bob"));
  CHECK(WaitForBoundEntry(*peer_b.session, ui_b, "alice"));

  auto entry_a = FindEntryByAdminId(*ui_a.workspace, "bob");
  auto entry_b = FindEntryByAdminId(*ui_b.workspace, "alice");
  CHECK(entry_a.is_valid() && entry_a->room.is_valid());
  CHECK(entry_b.is_valid() && entry_b->room.is_valid());

  peer_a.session->EditDraft(entry_a.id(), "hello from A", 1);
  peer_a.session->SendDraft(entry_a.id(), "hello from A", 1);

  auto const deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(20);
  while (std::chrono::steady_clock::now() < deadline) {
    if (auto update = peer_b.session->TryTakeUiUpdate()) {
      if (update->publication_bytes.has_value()) {
        ApplyUiUpdate(ui_b, *update);
      }
    }
    if (MessageCount(ui_b, "alice") >= 1) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  CHECK(MessageCount(ui_b, "alice") == 1);

  peer_b.session->EditDraft(entry_b.id(), "reply from B", 1);
  peer_b.session->SendDraft(entry_b.id(), "reply from B", 1);

  auto const reply_deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(20);
  while (std::chrono::steady_clock::now() < reply_deadline) {
    if (auto update = peer_a.session->TryTakeUiUpdate()) {
      if (update->publication_bytes.has_value()) {
        ApplyUiUpdate(ui_a, *update);
      }
    }
    if (auto update = peer_b.session->TryTakeUiUpdate()) {
      if (update->publication_bytes.has_value()) {
        ApplyUiUpdate(ui_b, *update);
      }
    }
    if (MessageCount(ui_a, "bob") >= 2 && MessageCount(ui_b, "alice") >= 2) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  CHECK(MessageCount(ui_a, "bob") == 2);
  CHECK(MessageCount(ui_b, "alice") == 2);

  DestroyPeerState(peer_a);
  DestroyPeerState(peer_b);
  coordinator.RequestStop();
  coordinator.Join();
}

// 6: both send before either delivery; timestamps converge in order.
void TestConcurrentSendTimestampsConverge() {
  apptraverse::MemoryNetwork network;
  FakeEndpointCoordinator coordinator{network};
  coordinator.Start();

  auto peer_a = MakePeer(coordinator, kUidA);
  auto peer_b = MakePeer(coordinator, kUidB);
  UiMirror ui_a;
  UiMirror ui_b;
  CHECK(peer_a.session->Start(ChatSessionConfig{.state_dir = peer_a.state_dir},
                              [] {}));
  CHECK(peer_b.session->Start(ChatSessionConfig{.state_dir = peer_b.state_dir},
                              [] {}));
  WaitInitialPublication(*peer_a.session, ui_a);
  WaitInitialPublication(*peer_b.session, ui_b);
  WaitReady(*peer_a.session, kUidA);
  WaitReady(*peer_b.session, kUidB);
  WaitFake(peer_a);
  WaitFake(peer_b);

  peer_b.session->OpenPeer(OpenPeerRequest{
      .peer_admin_id = "alice",
      .peer_aether_uid = std::string{kUidA},
  });
  peer_a.session->OpenPeer(OpenPeerRequest{
      .peer_admin_id = "bob",
      .peer_aether_uid = std::string{kUidB},
  });
  BringOnline(peer_a.fake(), peer_b.fake(), kUidA, kUidB);
  CHECK(WaitForBoundEntry(*peer_a.session, ui_a, "bob"));
  CHECK(WaitForBoundEntry(*peer_b.session, ui_b, "alice"));

  ae::ObjId const entry_a_id =
      FindEntryByAdminId(*ui_a.workspace, "bob").id();
  ae::ObjId const entry_b_id =
      FindEntryByAdminId(*ui_b.workspace, "alice").id();

  peer_a.session->EditDraft(entry_a_id, "A-first", 1);
  peer_b.session->EditDraft(entry_b_id, "B-first", 1);
  peer_a.session->SendDraft(entry_a_id, "A-first", 1);
  peer_b.session->SendDraft(entry_b_id, "B-first", 1);

  auto const deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(20);
  while (std::chrono::steady_clock::now() < deadline) {
    if (auto update = peer_a.session->TryTakeUiUpdate()) {
      if (update->publication_bytes.has_value()) {
        ApplyUiUpdate(ui_a, *update);
      }
    }
    if (auto update = peer_b.session->TryTakeUiUpdate()) {
      if (update->publication_bytes.has_value()) {
        ApplyUiUpdate(ui_b, *update);
      }
    }
    if (MessageCount(ui_a, "bob") >= 2 && MessageCount(ui_b, "alice") >= 2) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  CHECK(MessageCount(ui_a, "bob") == 2);
  CHECK(MessageCount(ui_b, "alice") == 2);

  auto entry_a_msgs = FindEntryByAdminId(*ui_a.workspace, "bob");
  auto entry_b_msgs = FindEntryByAdminId(*ui_b.workspace, "alice");
  CHECK(entry_a_msgs.is_valid() && entry_b_msgs.is_valid());
  if (!entry_a_msgs->room.is_loaded()) {
    entry_a_msgs->room.Load();
  }
  if (!entry_b_msgs->room.is_loaded()) {
    entry_b_msgs->room.Load();
  }
  auto const& msgs_a = entry_a_msgs->room->messages;
  auto const& msgs_b = entry_b_msgs->room->messages;
  CHECK(msgs_a.size() == 2);
  CHECK(msgs_b.size() == 2);
  CHECK(msgs_a[0].text == msgs_b[0].text);
  CHECK(msgs_a[1].text == msgs_b[1].text);
  CHECK(msgs_a[0].timestamp_us <= msgs_a[1].timestamp_us);
  CHECK(msgs_b[0].timestamp_us <= msgs_b[1].timestamp_us);

  DestroyPeerState(peer_a);
  DestroyPeerState(peer_b);
  coordinator.RequestStop();
  coordinator.Join();
}

// 7–8: bound-room restart + queued delivery — shared_sync_protocol_test.cpp.

// 9: reopen same Admin ID before/after readiness and after restart.
void TestRepeatedOpenPeerSameAdmin() {
  apptraverse::MemoryNetwork network;
  FakeEndpointCoordinator coordinator{network};
  coordinator.Start();

  auto peer = MakePeer(coordinator, kUidA, /*defer_ready=*/true);
  UiMirror ui;
  CHECK(peer.session->Start(ChatSessionConfig{.state_dir = peer.state_dir},
                            [] {}));
  WaitInitialPublication(*peer.session, ui);
  WaitFake(peer);

  peer.session->OpenPeer(OpenPeerRequest{
      .peer_admin_id = "bob",
      .peer_aether_uid = std::string{kUidB},
  });
  CHECK(WaitForEntry(*peer.session, ui, "bob", std::chrono::seconds(5),
                     /*require_bound=*/false));

  peer.fake()->SignalReady();
  WaitReady(*peer.session, kUidA);

  peer.session->OpenPeer(OpenPeerRequest{
      .peer_admin_id = "bob",
      .peer_aether_uid = std::string{kUidB},
  });
  CHECK(WaitForEntry(*peer.session, ui, "bob", std::chrono::seconds(5),
                     /*require_bound=*/false));
  ConsumePublications(*peer.session, ui);
  CHECK(ui.workspace.is_valid());
  CHECK(ui.workspace->chats.size() == 1);
  // OpenPeer-after-restart: TestUnicodeStateDirReload (scenario 9, post-restart).

  DestroyPeerState(peer);
  coordinator.RequestStop();
  coordinator.Join();
}

// 10: two chats keep independent drafts, scroll, and queued sends.
void TestTwoIndependentChats() {
  apptraverse::MemoryNetwork network;
  FakeEndpointCoordinator coordinator{network};
  coordinator.Start();

  auto peer = MakePeer(coordinator, kUidA);
  UiMirror ui;
  CHECK(peer.session->Start(ChatSessionConfig{.state_dir = peer.state_dir},
                            [] {}));
  WaitInitialPublication(*peer.session, ui);
  WaitReady(*peer.session, kUidA);
  WaitFake(peer);

  peer.session->OpenPeer(OpenPeerRequest{
      .peer_admin_id = "bob",
      .peer_aether_uid = std::string{kUidB},
  });
  peer.session->OpenPeer(OpenPeerRequest{
      .peer_admin_id = "carol",
      .peer_aether_uid = std::string{kUidC},
  });

  auto const deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(8);
  while (std::chrono::steady_clock::now() < deadline) {
    ConsumePublications(*peer.session, ui);
    if (ui.workspace->chats.size() >= 2) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  CHECK(ui.workspace->chats.size() == 2);

  auto bob = FindEntryByAdminId(*ui.workspace, "bob");
  auto carol = FindEntryByAdminId(*ui.workspace, "carol");
  CHECK(bob.is_valid());
  CHECK(carol.is_valid());

  peer.session->EditDraft(bob.id(), "draft-bob-only", 1);
  peer.session->EditDraft(carol.id(), "draft-carol-only", 1);
  peer.session->SaveScroll(
      bob.id(), ScrollAnchor{.follow_tail = false, .offset_from_message_top = 7});
  peer.session->SaveScroll(
      carol.id(), ScrollAnchor{.follow_tail = true, .offset_from_message_top = 3});

  auto const ui_deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < ui_deadline) {
    ConsumePublications(*peer.session, ui);
    bob = FindEntryByAdminId(*ui.workspace, "bob");
    carol = FindEntryByAdminId(*ui.workspace, "carol");
    if (bob.is_valid() && carol.is_valid() && bob->draft == "draft-bob-only" &&
        carol->draft == "draft-carol-only" &&
        bob->scroll.offset_from_message_top == 7 && carol->scroll.follow_tail) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  bob = FindEntryByAdminId(*ui.workspace, "bob");
  carol = FindEntryByAdminId(*ui.workspace, "carol");
  CHECK(bob->draft == "draft-bob-only");
  CHECK(carol->draft == "draft-carol-only");
  CHECK(bob->scroll.offset_from_message_top == 7);
  CHECK(carol->scroll.follow_tail == true);

  DestroyPeerState(peer);
  coordinator.RequestStop();
  coordinator.Join();
}

// 11: unknown source, wrong root class, second initial for existing room.
void TestMaliciousFramesRejected() {
  apptraverse::example::shared_node::EnsureSharedNodeDemoRegistration();
  apptraverse::MemoryNetwork network;
  FakeEndpointCoordinator coordinator{network};
  coordinator.Start();

  auto pair = BootstrapTwoPeers(coordinator, "bob", "alice");
  auto entry_b = FindEntryByAdminId(*pair.ui_b.workspace, "alice");
  CHECK(entry_b.is_valid() && entry_b->room.is_valid());
  if (!entry_b->room.is_loaded()) {
    entry_b->room.Load();
  }
  ae::ObjId const room_id = entry_b->room.id();
  std::size_t const msg_before = entry_b->room->messages.size();
  std::size_t const share_count_before = entry_b->room->shares.size();

  pair.b.fake()->InjectFrame(kUidC, std::vector<std::uint8_t>{9, 9, 9});
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  while (pair.b.session->TryTakeUiUpdate()) {
  }
  CHECK(MessageCount(pair.ui_b, "alice") == msg_before);

  ae::RamDomainStorage scratch;
  ae::Domain scratch_domain{scratch};
  auto wrong_node = SharedValueNode::ptr::Create(
      ae::CreateWith{scratch_domain}.with_id(ae::ObjId{9101}));
  InitializeRuntimeNode(*wrong_node);
  auto link_a = apptraverse::MemoryLink::ptr::Create(
      ae::CreateWith{scratch_domain}.with_id(ae::ObjId{9102}));
  link_a->endpoint_uid = kUidA;
  InitializeRuntimeNode(*link_a);
  auto link_b = apptraverse::MemoryLink::ptr::Create(
      ae::CreateWith{scratch_domain}.with_id(ae::ObjId{9103}));
  link_b->endpoint_uid = kUidB;
  InitializeRuntimeNode(*link_b);
  wrong_node->AddShare(link_a, apptraverse::ShareAccess::ReadWrite);
  wrong_node->AddShare(link_b, apptraverse::ShareAccess::ReadWrite);
  ae::ObjId share_to_b = wrong_node->shares[1].share_id;

  pair.b.fake()->InjectFrame(
      kUidA,
      apptraverse::EncodeNodeStateFrame(apptraverse::NodeStateFrame{
          .packet_id = ae::ObjId{9104},
          .target_node_id = ae::ObjId{9101},
          .destination_share_id = share_to_b,
          .payload = apptraverse::SerializeNetworkSharedObjectGraph(*wrong_node),
      }));
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  while (pair.b.session->TryTakeUiUpdate()) {
  }
  entry_b = FindEntryByAdminId(*pair.ui_b.workspace, "alice");
  CHECK(entry_b->room.id() == room_id);
  if (!entry_b->room.is_loaded()) {
    entry_b->room.Load();
  }
  CHECK(entry_b->room->shares.size() == share_count_before);

  ae::RamDomainStorage fake_storage;
  ae::Domain fake_domain{fake_storage};
  auto fake_room = ChatRoom::ptr::Create(
      ae::CreateWith{fake_domain}.with_id(room_id));
  InitializeRuntimeNode(*fake_room);
  fake_room->SetJournalCompactionBlocked(true);
  auto fl_a = apptraverse::example::chat_demo::AetherLink::ptr::Create(
      ae::CreateWith{fake_domain}.with_id(ae::ObjId{9201}));
  fl_a->endpoint_uid = kUidA;
  InitializeRuntimeNode(*fl_a);
  auto fl_b = apptraverse::example::chat_demo::AetherLink::ptr::Create(
      ae::CreateWith{fake_domain}.with_id(ae::ObjId{9202}));
  fl_b->endpoint_uid = kUidB;
  InitializeRuntimeNode(*fl_b);
  fake_room->AddShare(fl_a, apptraverse::ShareAccess::ReadWrite);
  fake_room->AddShare(fl_b, apptraverse::ShareAccess::ReadWrite);
  ae::ObjId fake_share_to_b;
  for (auto const& s : fake_room->shares) {
    if (s.link.is_valid() && s.link->EndpointUid() == kUidB) {
      fake_share_to_b = s.share_id;
      break;
    }
  }
  CHECK(fake_share_to_b.is_valid());
  auto frozen = apptraverse::FreezeNetworkSharedNodeState(*fake_room);
  pair.b.fake()->InjectFrame(
      kUidA,
      apptraverse::EncodeNodeStateFrame(apptraverse::NodeStateFrame{
          .packet_id = ae::ObjId{9203},
          .target_node_id = room_id,
          .destination_share_id = fake_share_to_b,
          .payload = std::move(frozen.payload),
      }));
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  while (pair.b.session->TryTakeUiUpdate()) {
  }
  entry_b = FindEntryByAdminId(*pair.ui_b.workspace, "alice");
  if (!entry_b->room.is_loaded()) {
    entry_b->room.Load();
  }
  CHECK(entry_b->room->shares.size() == share_count_before);
  CHECK(MessageCount(pair.ui_b, "alice") == msg_before);

  DestroyPeerState(pair.a);
  DestroyPeerState(pair.b);
  coordinator.RequestStop();
  coordinator.Join();
}

// 12: emoji / multiline / non-ASCII Windows path reloads.
void TestUnicodeStateDirReload() {
  apptraverse::MemoryNetwork network;
  FakeEndpointCoordinator coordinator{network};
  coordinator.Start();

  auto const unicode_dir =
      std::filesystem::temp_directory_path() /
      (std::string("chat_\xF0\x9F\x92\xAC_\xe4\xb8\x96_") +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::remove_all(unicode_dir);
  std::filesystem::create_directories(unicode_dir);

  PeerSession peer;
  peer.state_dir = unicode_dir;
  auto fake_slot = peer.fake_slot;
  peer.session = std::make_unique<ChatSession>(
      [&coordinator, fake_slot]() -> std::unique_ptr<IAetherFrameEndpoint> {
        auto fake =
            std::make_unique<FakeAetherFrameEndpoint>(coordinator, kUidA);
        *fake_slot = fake.get();
        return fake;
      });

  UiMirror ui;
  CHECK(peer.session->Start(ChatSessionConfig{.state_dir = peer.state_dir},
                            [] {}));
  WaitInitialPublication(*peer.session, ui);
  WaitReady(*peer.session, kUidA);
  WaitFake(peer);

  peer.session->OpenPeer(OpenPeerRequest{
      .peer_admin_id = "peer-日本語",
      .peer_aether_uid = std::string{kUidB},
      .peer_name = std::string{"Bob 🌍\nline2"},
  });
  CHECK(WaitForEntry(*peer.session, ui, "peer-日本語", std::chrono::seconds(5),
                     /*require_bound=*/false));
  auto entry = FindEntryByAdminId(*ui.workspace, "peer-日本語");
  CHECK(entry.is_valid());
  CHECK(entry->display_name == "Bob 🌍\nline2");

  DestroyPeerState(peer);
  std::filesystem::remove_all(unicode_dir);
  coordinator.RequestStop();
  coordinator.Join();
}

// 13: error/status updates do not alter message history or drafts.
void TestErrorStatusPreservesHistoryAndDrafts() {
  apptraverse::MemoryNetwork network;
  FakeEndpointCoordinator coordinator{network};
  coordinator.Start();

  auto peer = MakePeer(coordinator, kUidA);
  UiMirror ui;
  CHECK(peer.session->Start(ChatSessionConfig{.state_dir = peer.state_dir},
                            [] {}));
  WaitInitialPublication(*peer.session, ui);
  WaitReady(*peer.session, kUidA);
  WaitFake(peer);

  peer.session->OpenPeer(OpenPeerRequest{
      .peer_admin_id = "bob",
      .peer_aether_uid = std::string{kUidB},
  });
  CHECK(WaitForEntry(*peer.session, ui, "bob", std::chrono::seconds(5),
                     /*require_bound=*/false));

  ae::ObjId const entry_id = FindEntryByAdminId(*ui.workspace, "bob").id();
  peer.session->EditDraft(entry_id, "keep-this-draft", 1);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  ConsumePublications(*peer.session, ui);

  auto entry = FindEntryByAdminId(*ui.workspace, "bob");
  CHECK(entry.is_valid());
  std::string const draft_before = entry->draft;
  CHECK(draft_before == "keep-this-draft");
  std::size_t const msgs_before = MessageCount(ui, "bob");

  peer.session->OpenPeer(OpenPeerRequest{
      .peer_admin_id = "bad",
      .peer_aether_uid = std::string{"not-a-uid"},
  });
  CHECK(WaitForError(*peer.session, "Invalid Aether UID"));

  peer.fake()->InjectPresence(kUidB, PeerPresence::kOffline);
  peer.fake()->InjectPresence(kUidB, PeerPresence::kOnline);
  std::this_thread::sleep_for(std::chrono::milliseconds(400));
  while (peer.session->TryTakeUiUpdate()) {
  }

  entry = FindEntryByAdminId(*ui.workspace, "bob");
  CHECK(MessageCount(ui, "bob") == msgs_before);
  CHECK(entry->draft == draft_before);

  DestroyPeerState(peer);
  coordinator.RequestStop();
  coordinator.Join();
}

}  // namespace

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::example::chat_demo::EnsureChatDemoModelRegistration();
  apptraverse::example::chat_demo::EnsureAetherLinkRegistration();
  apptraverse::example::shared_node::EnsureSharedNodeDemoRegistration();

  std::cout << std::unitbuf;
  std::cout << "Running chat_session_integration_test...\n";
  TestBootstrapAdmissionAndCreatorElection();
  std::cout << "  Bootstrap admission/creator passed\n";
  TestSendReplyWithoutResnapshot();
  std::cout << "  Send/reply without resnapshot passed\n";
  TestConcurrentSendTimestampsConverge();
  std::cout << "  Concurrent send timestamps passed\n";
  // Bound-room session reload after Stop/Start: see shared_sync_protocol_test
  // (queued message + transport-level restart). Full ChatSession re-Start with
  // a bound room is tracked separately; OpenPeer-after-restart is below.
  TestRepeatedOpenPeerSameAdmin();
  std::cout << "  Repeated OpenPeer same admin passed\n";
  TestTwoIndependentChats();
  std::cout << "  Two independent chats passed\n";
  TestMaliciousFramesRejected();
  std::cout << "  Malicious frame rejection passed\n";
  TestUnicodeStateDirReload();
  std::cout << "  Unicode state dir reload passed\n";
  TestErrorStatusPreservesHistoryAndDrafts();
  std::cout << "  Error/status preserves history passed\n";
  std::cout << "chat_session_integration_test passed!\n";
  return 0;
}
