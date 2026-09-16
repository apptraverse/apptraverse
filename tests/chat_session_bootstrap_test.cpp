#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether_link.h"
#include "apptraverse/object_serialization.h"
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
using apptraverse::example::chat_demo::ChatWorkspace;
using apptraverse::example::chat_demo::IAetherFrameEndpoint;
using apptraverse::example::chat_demo::DemoRole;
using apptraverse::example::chat_demo::PeerPresence;
using apptraverse::example::chat_demo::SessionLifecycleState;
using apptraverse::example::chat_demo::test::FakeAetherFrameEndpoint;
using apptraverse::example::chat_demo::test::FakeEndpointCoordinator;

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
  std::unique_ptr<ae::RamDomainStorage> storage =
      std::make_unique<ae::RamDomainStorage>();
  std::unique_ptr<ae::Domain> domain;
  ChatWorkspace::ptr workspace;
};

void ConsumePublications(ChatSession& session, UiMirror& ui) {
  while (auto update = session.TryTakeUiUpdate()) {
    if (!update->publication_bytes.has_value() ||
        update->publication_bytes->empty()) {
      continue;
    }
    auto const& bytes = *update->publication_bytes;
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
      CHECK(ui.workspace);
    } else {
      apptraverse::ApplyStructuralPublicationAndUpdatePresenters(
          in, *ui.domain, *ui.storage, *ui.workspace);
    }
  }
}

void WaitInitialPublication(ChatSession& session, UiMirror& ui,
                            std::chrono::milliseconds timeout =
                                std::chrono::seconds(5)) {
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
               std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
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

ChatEntry::ptr FindEntryByPeerUid(ChatWorkspace& ws, std::string const& admin) {
  for (auto const& entry : ws.chats) {
    if (entry.is_valid() && entry->peer_uid == admin) {
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
      auto entry = FindEntryByPeerUid(*ui.workspace, admin);
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

bool WaitForError(ChatSession& session, std::string const& expected,
                  std::chrono::milliseconds timeout = std::chrono::seconds(3)) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (session.GetRuntimeStatus().error_text == expected) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}

struct PeerSession {
  std::filesystem::path state_dir;
  std::shared_ptr<FakeAetherFrameEndpoint*> fake_slot =
      std::make_shared<FakeAetherFrameEndpoint*>(nullptr);
  FakeAetherFrameEndpoint* fake() const { return *fake_slot; }
  std::unique_ptr<ChatSession> session;
};

PeerSession MakePeer(FakeEndpointCoordinator& coordinator,
                     std::string const& uid, bool defer_ready) {
  PeerSession peer;
  peer.state_dir = MakeTempDir("chat_boot");
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

void StopPeer(PeerSession& peer) {
  if (peer.session) {
    peer.session->RequestStop();
    peer.session->Join();
  }
  std::filesystem::remove_all(peer.state_dir);
}

void BringOnline(FakeAetherFrameEndpoint* a, FakeAetherFrameEndpoint* b,
                 std::string const& uid_a, std::string const& uid_b) {
  a->InjectPresence(uid_b, PeerPresence::kOnline);
  b->InjectPresence(uid_a, PeerPresence::kOnline);
}

FakeAetherFrameEndpoint* WaitFake(PeerSession& peer,
                                  std::chrono::milliseconds timeout =
                                      std::chrono::seconds(5)) {
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

void TestOpenPeerBeforeReadiness() {
  apptraverse::MemoryNetwork network;
  FakeEndpointCoordinator coordinator{network};
  coordinator.Start();

  auto peer = MakePeer(coordinator, kUidA, /*defer_ready=*/true);
  UiMirror ui;
  CHECK(peer.session->Start(ChatSessionConfig{.state_dir = peer.state_dir, .role = DemoRole::kClient},
                            [] {}));
  WaitInitialPublication(*peer.session, ui);
  WaitFake(peer);

  peer.session->SetHostUidInput(std::string{kUidB});
  peer.session->JoinHost();

  CHECK(WaitForEntry(*peer.session, ui, kUidB, std::chrono::seconds(5),
                     /*require_bound=*/false));
  {
    auto entry = FindEntryByPeerUid(*ui.workspace, kUidB);
    CHECK(entry.is_valid());
    CHECK(!entry->room.is_valid());
    CHECK(!entry->peer_link.is_valid());
  }

  peer.fake()->SignalReady();
  WaitReady(*peer.session, kUidA);

  CHECK(WaitForEntry(*peer.session, ui, kUidB, std::chrono::seconds(5),
                     /*require_bound=*/false));
  CHECK(ui.workspace->chats.size() == 1);

  StopPeer(peer);
  coordinator.RequestStop();
  coordinator.Join();
}

void TestTwoSessionsOneCreatorWaitingBindsWithoutPlaceholderLink() {
  apptraverse::MemoryNetwork network;
  FakeEndpointCoordinator coordinator{network};
  coordinator.Start();

  auto peer_a = MakePeer(coordinator, kUidA, /*defer_ready=*/false);
  auto peer_b = MakePeer(coordinator, kUidB, /*defer_ready=*/false);
  UiMirror ui_a;
  UiMirror ui_b;
  CHECK(peer_a.session->Start(ChatSessionConfig{.state_dir = peer_a.state_dir, .role = DemoRole::kHost},
                              [] {}));
  CHECK(peer_b.session->Start(ChatSessionConfig{.state_dir = peer_b.state_dir, .role = DemoRole::kClient},
                              [] {}));
  WaitInitialPublication(*peer_a.session, ui_a);
  WaitInitialPublication(*peer_b.session, ui_b);
  WaitReady(*peer_a.session, kUidA);
  WaitReady(*peer_b.session, kUidB);
  auto* fake_a = WaitFake(peer_a);
  auto* fake_b = WaitFake(peer_b);

  peer_b.session->SetHostUidInput(std::string{kUidA});
peer_b.session->JoinHost();


  CHECK(WaitForEntry(*peer_b.session, ui_b, kUidA, std::chrono::seconds(5),
                     /*require_bound=*/false));
  {
    auto entry = FindEntryByPeerUid(*ui_b.workspace, kUidA);
    CHECK(!entry->peer_link.is_valid());
    CHECK(!entry->room.is_valid());
  }

  

  BringOnline(fake_a, fake_b, kUidA, kUidB);

  CHECK(WaitForEntry(*peer_a.session, ui_a, kUidB, std::chrono::seconds(10),
                     /*require_bound=*/true));
  CHECK(WaitForEntry(*peer_b.session, ui_b, kUidA, std::chrono::seconds(10),
                     /*require_bound=*/true));

  auto entry_a = FindEntryByPeerUid(*ui_a.workspace, kUidB);
  auto entry_b = FindEntryByPeerUid(*ui_b.workspace, kUidA);
  CHECK(entry_a->room.id() == entry_b->room.id());
  CHECK(entry_b->peer_link->EndpointUid() == kUidA);

  
  peer_b.session->SetHostUidInput(std::string{kUidA});
peer_b.session->JoinHost();

  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  ConsumePublications(*peer_a.session, ui_a);
  CHECK(ui_a.workspace->chats.size() == 1);

  StopPeer(peer_a);
  StopPeer(peer_b);
  coordinator.RequestStop();
  coordinator.Join();
}

void TestInvalidAndConflictingUid() {
  apptraverse::MemoryNetwork network;
  FakeEndpointCoordinator coordinator{network};
  coordinator.Start();

  auto peer = MakePeer(coordinator, kUidA, /*defer_ready=*/false);
  UiMirror ui;
  CHECK(peer.session->Start(ChatSessionConfig{.state_dir = peer.state_dir, .role = DemoRole::kClient},
                            [] {}));
  WaitInitialPublication(*peer.session, ui);
  WaitReady(*peer.session, kUidA);

  peer.session->SetHostUidInput("not-a-uid");
  peer.session->JoinHost();
  CHECK(WaitForError(*peer.session, "Invalid Host UID"));

  peer.session->SetHostUidInput(std::string{kUidA});
  peer.session->JoinHost();
  CHECK(WaitForError(*peer.session, "Invalid Host UID"));

  peer.session->SetHostUidInput(std::string{kUidB});
  peer.session->JoinHost();
  CHECK(WaitForEntry(*peer.session, ui, kUidB, std::chrono::seconds(5),
                     /*require_bound=*/false));

  StopPeer(peer);
  coordinator.RequestStop();
  coordinator.Join();
}

void TestUnsolicitedSenderDoesNotAuthorizeRoom() {
  apptraverse::MemoryNetwork network;
  FakeEndpointCoordinator coordinator{network};
  coordinator.Start();

  auto peer_b = MakePeer(coordinator, kUidB, /*defer_ready=*/false);
  UiMirror ui;
  CHECK(peer_b.session->Start(ChatSessionConfig{.state_dir = peer_b.state_dir, .role = DemoRole::kClient},
                              [] {}));
  WaitInitialPublication(*peer_b.session, ui);
  WaitReady(*peer_b.session, kUidB);
  WaitFake(peer_b);

  peer_b.session->SetHostUidInput(std::string{kUidA});
peer_b.session->JoinHost();

  CHECK(WaitForEntry(*peer_b.session, ui, kUidA, std::chrono::seconds(5),
                     /*require_bound=*/false));

  peer_b.fake()->InjectFrame(kUidC, std::vector<std::uint8_t>{1, 2, 3, 4});
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  ConsumePublications(*peer_b.session, ui);
  {
    auto entry = FindEntryByPeerUid(*ui.workspace, kUidA);
    CHECK(entry.is_valid());
    CHECK(!entry->room.is_valid());
    CHECK(!entry->peer_link.is_valid());
  }

  StopPeer(peer_b);
  coordinator.RequestStop();
  coordinator.Join();
}

}  // namespace

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::example::chat_demo::EnsureChatDemoModelRegistration();
  apptraverse::example::chat_demo::EnsureAetherLinkRegistration();

  TestOpenPeerBeforeReadiness();
  std::cout << "  OpenPeer before readiness passed\n";
  TestTwoSessionsOneCreatorWaitingBindsWithoutPlaceholderLink();
  std::cout << "  Two-session bootstrap passed\n";
  TestInvalidAndConflictingUid();
  std::cout << "  Invalid/conflicting UID passed\n";
  TestUnsolicitedSenderDoesNotAuthorizeRoom();
  std::cout << "  Unsolicited sender rejected\n";
  std::cout << "chat_session_bootstrap_test passed!\n";
  return 0;
}
