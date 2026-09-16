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
using apptraverse::example::chat_demo::OpenPeerRequest;
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
  ae::RamDomainStorage storage;
  std::unique_ptr<ae::Domain> domain;
  ChatWorkspace::ptr workspace;
};

void ConsumePublications(ChatSession& session, UiMirror& ui) {
  for (;;) {
    auto bytes = session.publication_channel().TakePublishedCopy();
    if (bytes.empty()) {
      break;
    }
    apptraverse::ByteSource in;
    in.data = bytes.data();
    in.size = bytes.size();
    if (!ui.workspace.is_valid()) {
      ui.domain = std::make_unique<ae::Domain>(ui.storage);
      auto root =
          apptraverse::LoadInitialPublication(in, *ui.domain, ui.storage);
      CHECK(root);
      CHECK(root->GetClassId() == ChatWorkspace::kClassId);
      ui.workspace = ChatWorkspace::ptr::MakeFromThis(
          static_cast<ChatWorkspace*>(root.get()));
      CHECK(ui.workspace);
    } else {
      apptraverse::ApplyStructuralPublication(in, *ui.domain, ui.storage);
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
  CHECK(peer.session->Start(ChatSessionConfig{.state_dir = peer.state_dir},
                            [] {}));
  WaitInitialPublication(*peer.session, ui);
  WaitFake(peer);

  peer.session->OpenPeer(OpenPeerRequest{
      .peer_admin_id = " bob ",
      .peer_aether_uid = std::string{kUidB},
      .peer_name = std::string{"Bob"},
  });

  CHECK(WaitForEntry(*peer.session, ui, "bob", std::chrono::seconds(5),
                     /*require_bound=*/false));
  {
    auto entry = FindEntryByAdminId(*ui.workspace, "bob");
    CHECK(entry.is_valid());
    CHECK(!entry->room.is_valid());
    CHECK(!entry->peer_link.is_valid());
  }

  peer.fake()->SignalReady();
  WaitReady(*peer.session, kUidA);

  CHECK(WaitForEntry(*peer.session, ui, "bob", std::chrono::seconds(5),
                     /*require_bound=*/true));
  {
    auto entry = FindEntryByAdminId(*ui.workspace, "bob");
    CHECK(entry->peer_link->EndpointUid() == kUidB);
  }

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
  CHECK(peer_a.session->Start(ChatSessionConfig{.state_dir = peer_a.state_dir},
                              [] {}));
  CHECK(peer_b.session->Start(ChatSessionConfig{.state_dir = peer_b.state_dir},
                              [] {}));
  WaitInitialPublication(*peer_a.session, ui_a);
  WaitInitialPublication(*peer_b.session, ui_b);
  WaitReady(*peer_a.session, kUidA);
  WaitReady(*peer_b.session, kUidB);
  auto* fake_a = WaitFake(peer_a);
  auto* fake_b = WaitFake(peer_b);

  peer_b.session->OpenPeer(OpenPeerRequest{
      .peer_admin_id = "alice",
      .peer_aether_uid = std::string{kUidA},
      .peer_name = std::string{"Alice"},
  });

  CHECK(WaitForEntry(*peer_b.session, ui_b, "alice", std::chrono::seconds(5),
                     /*require_bound=*/false));
  {
    auto entry = FindEntryByAdminId(*ui_b.workspace, "alice");
    CHECK(!entry->peer_link.is_valid());
    CHECK(!entry->room.is_valid());
  }

  peer_a.session->OpenPeer(OpenPeerRequest{
      .peer_admin_id = "bob",
      .peer_aether_uid = std::string{kUidB},
      .peer_name = std::string{"Bob"},
  });

  BringOnline(fake_a, fake_b, kUidA, kUidB);

  CHECK(WaitForEntry(*peer_a.session, ui_a, "bob", std::chrono::seconds(10),
                     /*require_bound=*/true));
  CHECK(WaitForEntry(*peer_b.session, ui_b, "alice", std::chrono::seconds(10),
                     /*require_bound=*/true));

  auto entry_a = FindEntryByAdminId(*ui_a.workspace, "bob");
  auto entry_b = FindEntryByAdminId(*ui_b.workspace, "alice");
  CHECK(entry_a->room.id() == entry_b->room.id());
  CHECK(entry_b->peer_link->EndpointUid() == kUidA);

  peer_a.session->OpenPeer(OpenPeerRequest{
      .peer_admin_id = "bob",
      .peer_aether_uid = std::string{kUidB},
  });
  peer_b.session->OpenPeer(OpenPeerRequest{
      .peer_admin_id = "alice",
      .peer_aether_uid = std::string{kUidA},
  });
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
  CHECK(peer.session->Start(ChatSessionConfig{.state_dir = peer.state_dir},
                            [] {}));
  WaitInitialPublication(*peer.session, ui);
  WaitReady(*peer.session, kUidA);

  peer.session->OpenPeer(OpenPeerRequest{
      .peer_admin_id = "bad",
      .peer_aether_uid = std::string{"not-a-uid"},
  });
  CHECK(WaitForError(*peer.session, "Invalid Aether UID"));

  peer.session->OpenPeer(OpenPeerRequest{
      .peer_admin_id = "bob",
      .peer_aether_uid = std::string{kUidB},
  });
  CHECK(WaitForEntry(*peer.session, ui, "bob", std::chrono::seconds(5),
                     /*require_bound=*/true));

  peer.session->OpenPeer(OpenPeerRequest{
      .peer_admin_id = "bob",
      .peer_aether_uid = std::string{kUidC},
  });
  CHECK(WaitForError(*peer.session, "Endpoint conflict for bound chat"));

  peer.session->OpenPeer(OpenPeerRequest{
      .peer_admin_id = "self",
      .peer_aether_uid = std::string{kUidA},
  });
  CHECK(WaitForError(*peer.session, "Cannot open chat with self Aether UID"));

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
  CHECK(peer_b.session->Start(ChatSessionConfig{.state_dir = peer_b.state_dir},
                              [] {}));
  WaitInitialPublication(*peer_b.session, ui);
  WaitReady(*peer_b.session, kUidB);
  WaitFake(peer_b);

  peer_b.session->OpenPeer(OpenPeerRequest{
      .peer_admin_id = "alice",
      .peer_aether_uid = std::string{kUidA},
  });
  CHECK(WaitForEntry(*peer_b.session, ui, "alice", std::chrono::seconds(5),
                     /*require_bound=*/false));

  peer_b.fake()->InjectFrame(kUidC, std::vector<std::uint8_t>{1, 2, 3, 4});
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  ConsumePublications(*peer_b.session, ui);
  {
    auto entry = FindEntryByAdminId(*ui.workspace, "alice");
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
