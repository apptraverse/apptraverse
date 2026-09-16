#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether_link.h"
#include "apptraverse/directory_domain_storage.h"
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
using apptraverse::example::chat_demo::ChatPublicationKind;
using apptraverse::example::chat_demo::ChatSession;
using apptraverse::example::chat_demo::ChatSessionConfig;
using apptraverse::example::chat_demo::ChatUiUpdate;
using apptraverse::example::chat_demo::ChatWorkspace;
using apptraverse::example::chat_demo::IAetherFrameEndpoint;
using apptraverse::example::chat_demo::OpenPeerRequest;
using apptraverse::example::chat_demo::PeerPresence;
using apptraverse::example::chat_demo::SessionLifecycleState;
using apptraverse::example::chat_demo::test::FakeAetherFrameEndpoint;
using apptraverse::example::chat_demo::test::FakeEndpointCoordinator;

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

std::size_t CountModelStorageFiles(std::filesystem::path const& model_dir) {
  if (!std::filesystem::exists(model_dir)) {
    return 0;
  }
  std::size_t count = 0;
  for (auto const& entry :
       std::filesystem::recursive_directory_iterator(model_dir)) {
    if (entry.is_regular_file()) {
      ++count;
    }
  }
  return count;
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
    ui.workspace = ChatWorkspace::ptr::MakeFromThis(
        static_cast<ChatWorkspace*>(root.get()));
  } else {
    apptraverse::ApplyStructuralPublication(in, *ui.domain, ui.storage);
  }
}

std::optional<ChatUiUpdate> WaitForPublication(
    ChatSession& session, std::uint64_t min_serial = 1,
    std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (auto update = session.TryTakeUiUpdate()) {
      if (update->publication_bytes.has_value() &&
          !update->publication_bytes->empty() &&
          update->publication_serial >= min_serial) {
        return update;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return std::nullopt;
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

FakeAetherFrameEndpoint* WaitFake(std::shared_ptr<FakeAetherFrameEndpoint*>& slot,
                                  std::chrono::milliseconds timeout =
                                      std::chrono::seconds(5)) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (*slot != nullptr) {
      return *slot;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  CHECK(false && "fake endpoint was not created");
  return nullptr;
}

void BringOnline(FakeAetherFrameEndpoint* a, FakeAetherFrameEndpoint* b,
                 std::string const& uid_a, std::string const& uid_b) {
  a->InjectPresence(uid_b, PeerPresence::kOnline);
  b->InjectPresence(uid_a, PeerPresence::kOnline);
}

struct PeerSession {
  std::filesystem::path state_dir;
  std::shared_ptr<FakeAetherFrameEndpoint*> fake_slot =
      std::make_shared<FakeAetherFrameEndpoint*>(nullptr);
  std::unique_ptr<ChatSession> session;
};

PeerSession MakePeer(FakeEndpointCoordinator& coordinator, std::string const& uid) {
  PeerSession peer;
  peer.state_dir = MakeTempDir("chat_pub");
  auto fake_slot = peer.fake_slot;
  peer.session = std::make_unique<ChatSession>(
      [&coordinator, fake_slot, uid]() -> std::unique_ptr<IAetherFrameEndpoint> {
        auto fake =
            std::make_unique<FakeAetherFrameEndpoint>(coordinator, uid);
        *fake_slot = fake.get();
        return fake;
      });
  return peer;
}

void TestSnapshotRevisionFrozenWhileUnread() {
  apptraverse::MemoryNetwork network;
  FakeEndpointCoordinator coordinator{network};
  coordinator.Start();

  auto fake_slot = std::make_shared<FakeAetherFrameEndpoint*>(nullptr);
  auto state_dir = MakeTempDir("chat_pub_rev");
  ChatSession session([&coordinator, fake_slot]() {
    auto fake = std::make_unique<FakeAetherFrameEndpoint>(coordinator, kUidA);
    *fake_slot = fake.get();
    return fake;
  });

  CHECK(session.Start(ChatSessionConfig{.state_dir = state_dir}, [] {}));
  WaitFake(fake_slot);
  UiMirror ui;
  auto initial = WaitForPublication(session);
  CHECK(initial.has_value());
  ApplyUiUpdate(ui, *initial);
  WaitReady(session, kUidA);

  session.OpenPeer(OpenPeerRequest{
      .peer_admin_id = "bob",
      .peer_aether_uid = std::string{kUidB},
  });

  ChatEntry::ptr entry;
  auto const entry_deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < entry_deadline) {
    if (auto update = session.TryTakeUiUpdate()) {
      ApplyUiUpdate(ui, *update);
    }
    entry = FindEntryByAdminId(*ui.workspace, "bob");
    if (entry.is_valid()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  CHECK(entry.is_valid());

  session.EditDraft(entry.id(), "draft-one", 1);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  session.EditDraft(entry.id(), "draft-two", 2);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  auto consumed_p1 = session.TryTakeUiUpdate();
  CHECK(consumed_p1.has_value());
  CHECK(consumed_p1->publication_serial >= 2);
  auto rev_it = consumed_p1->processed_edit_revisions_by_entry.find(entry.id());
  CHECK(rev_it != consumed_p1->processed_edit_revisions_by_entry.end());
  CHECK(rev_it->second == 1);

  auto p2 = WaitForPublication(session, consumed_p1->publication_serial + 1);
  CHECK(p2.has_value());
  rev_it = p2->processed_edit_revisions_by_entry.find(entry.id());
  CHECK(rev_it != p2->processed_edit_revisions_by_entry.end());
  CHECK(rev_it->second == 2);

  session.RequestStop();
  session.Join();
  coordinator.RequestStop();
  coordinator.Join();
  std::filesystem::remove_all(state_dir);
}

void TestRemoteMessageUpdatesGuiMirror() {
  apptraverse::MemoryNetwork network;
  FakeEndpointCoordinator coordinator{network};
  coordinator.Start();

  auto peer_a = MakePeer(coordinator, kUidA);
  auto peer_b = MakePeer(coordinator, kUidB);
  UiMirror ui_b;
  CHECK(peer_a.session->Start(ChatSessionConfig{.state_dir = peer_a.state_dir}, [] {}));
  CHECK(peer_b.session->Start(ChatSessionConfig{.state_dir = peer_b.state_dir}, [] {}));
  WaitFake(peer_a.fake_slot);
  WaitFake(peer_b.fake_slot);
  WaitReady(*peer_a.session, kUidA);
  WaitReady(*peer_b.session, kUidB);

  peer_b.session->OpenPeer(OpenPeerRequest{
      .peer_admin_id = "alice",
      .peer_aether_uid = std::string{kUidA},
  });
  peer_a.session->OpenPeer(OpenPeerRequest{
      .peer_admin_id = "bob",
      .peer_aether_uid = std::string{kUidB},
  });

  BringOnline(*peer_a.fake_slot, *peer_b.fake_slot, kUidA, kUidB);

  auto initial_b = WaitForPublication(*peer_b.session);
  CHECK(initial_b.has_value());
  ApplyUiUpdate(ui_b, *initial_b);

  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
  bool room_bound = false;
  while (std::chrono::steady_clock::now() < deadline) {
    if (auto update = WaitForPublication(*peer_b.session, initial_b->publication_serial + 1,
                                         std::chrono::milliseconds(200))) {
      ApplyUiUpdate(ui_b, *update);
    }
    auto entry = FindEntryByAdminId(*ui_b.workspace, "alice");
    if (entry.is_valid() && entry->room.is_valid()) {
      room_bound = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  CHECK(room_bound);

  UiMirror ui_a;
  ApplyUiUpdate(ui_a, *WaitForPublication(*peer_a.session));
  ChatEntry::ptr entry_a;
  deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
  while (std::chrono::steady_clock::now() < deadline) {
    if (auto update = peer_a.session->TryTakeUiUpdate()) {
      ApplyUiUpdate(ui_a, *update);
    }
    entry_a = FindEntryByAdminId(*ui_a.workspace, "bob");
    if (entry_a.is_valid() && entry_a->room.is_valid()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  CHECK(entry_a.is_valid());
  CHECK(entry_a->room.is_valid());
  peer_a.session->EditDraft(entry_a.id(), "hello from A", 1);
  peer_a.session->SendDraft(entry_a.id(), "hello from A", 1);

  std::size_t message_count = 0;
  deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
  while (std::chrono::steady_clock::now() < deadline) {
    if (auto update = peer_b.session->TryTakeUiUpdate()) {
      if (update->publication_bytes.has_value()) {
        ApplyUiUpdate(ui_b, *update);
      }
      auto entry = FindEntryByAdminId(*ui_b.workspace, "alice");
      if (entry.is_valid() && entry->room.is_valid()) {
        message_count = entry->room->messages.size();
        if (message_count >= 1) {
          CHECK(entry->room->messages[0].text == "hello from A");
          break;
        }
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  CHECK(message_count == 1);

  peer_a.session->RequestStop();
  peer_a.session->Join();
  peer_b.session->RequestStop();
  peer_b.session->Join();
  coordinator.RequestStop();
  coordinator.Join();
  std::filesystem::remove_all(peer_a.state_dir);
  std::filesystem::remove_all(peer_b.state_dir);
}

void TestIdlePollingDoesNotRewrite() {
  apptraverse::MemoryNetwork network;
  FakeEndpointCoordinator coordinator{network};
  coordinator.Start();

  auto peer_a = MakePeer(coordinator, kUidA);
  auto peer_b = MakePeer(coordinator, kUidB);
  CHECK(peer_a.session->Start(ChatSessionConfig{.state_dir = peer_a.state_dir}, [] {}));
  CHECK(peer_b.session->Start(ChatSessionConfig{.state_dir = peer_b.state_dir}, [] {}));
  WaitFake(peer_a.fake_slot);
  WaitFake(peer_b.fake_slot);
  WaitReady(*peer_a.session, kUidA);
  WaitReady(*peer_b.session, kUidB);

  peer_b.session->OpenPeer(OpenPeerRequest{
      .peer_admin_id = "alice",
      .peer_aether_uid = std::string{kUidA},
  });
  peer_a.session->OpenPeer(OpenPeerRequest{
      .peer_admin_id = "bob",
      .peer_aether_uid = std::string{kUidB},
  });
  BringOnline(*peer_a.fake_slot, *peer_b.fake_slot, kUidA, kUidB);

  std::uint64_t last_serial = 0;
  auto const settle_deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (std::chrono::steady_clock::now() < settle_deadline) {
    if (auto update = peer_a.session->TryTakeUiUpdate()) {
      if (update->publication_bytes.has_value() &&
          !update->publication_bytes->empty()) {
        last_serial = update->publication_serial;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  std::this_thread::sleep_for(std::chrono::seconds(2));

  auto const model_dir = peer_a.state_dir / "model";
  auto const files_before = CountModelStorageFiles(model_dir);

  for (int i = 0; i < 40; ++i) {
    (*peer_a.fake_slot)->InjectPresence(kUidB, PeerPresence::kOnline);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  auto const files_after = CountModelStorageFiles(model_dir);
  CHECK(files_after == files_before);

  while (auto update = peer_a.session->TryTakeUiUpdate()) {
    if (update->publication_bytes.has_value() &&
        !update->publication_bytes->empty()) {
      CHECK(update->publication_serial <= last_serial);
    }
  }

  peer_a.session->RequestStop();
  peer_a.session->Join();
  peer_b.session->RequestStop();
  peer_b.session->Join();
  coordinator.RequestStop();
  coordinator.Join();
  std::filesystem::remove_all(peer_a.state_dir);
  std::filesystem::remove_all(peer_b.state_dir);
}

void TestShutdownWithoutUiConsumption() {
  apptraverse::MemoryNetwork network;
  FakeEndpointCoordinator coordinator{network};
  coordinator.Start();

  auto fake_slot = std::make_shared<FakeAetherFrameEndpoint*>(nullptr);
  auto state_dir = MakeTempDir("chat_pub_noui");
  ChatSession session([&coordinator, fake_slot]() {
    auto fake = std::make_unique<FakeAetherFrameEndpoint>(coordinator, kUidA);
    *fake_slot = fake.get();
    return fake;
  });

  CHECK(session.Start(ChatSessionConfig{.state_dir = state_dir}, [] {}));
  WaitFake(fake_slot);
  WaitReady(session, kUidA);

  session.OpenPeer(OpenPeerRequest{
      .peer_admin_id = "bob",
      .peer_aether_uid = std::string{kUidB},
  });
  session.EditDraft(ae::ObjId{}, "queued", 1);

  session.RequestStop();
  session.Join();
  CHECK(session.GetRuntimeStatus().lifecycle_state == SessionLifecycleState::kStopped);

  coordinator.RequestStop();
  coordinator.Join();
  std::filesystem::remove_all(state_dir);
}

void TestPresenceStatusWithoutGraphRewrite() {
  apptraverse::MemoryNetwork network;
  FakeEndpointCoordinator coordinator{network};
  coordinator.Start();

  auto fake_slot = std::make_shared<FakeAetherFrameEndpoint*>(nullptr);
  auto state_dir = MakeTempDir("chat_pub_presence");
  ChatSession session([&coordinator, fake_slot]() {
    auto fake = std::make_unique<FakeAetherFrameEndpoint>(coordinator, kUidA);
    *fake_slot = fake.get();
    return fake;
  });

  CHECK(session.Start(ChatSessionConfig{.state_dir = state_dir}, [] {}));
  auto* fake = WaitFake(fake_slot);
  WaitForPublication(session);
  WaitReady(session, kUidA);

  std::uint64_t last_structural_serial = 0;
  while (auto update = session.TryTakeUiUpdate()) {
    if (update->publication_bytes.has_value() &&
        !update->publication_bytes->empty()) {
      last_structural_serial = update->publication_serial;
    }
  }

  fake->InjectPresence(kUidB, PeerPresence::kOnline);
  std::optional<ChatUiUpdate> status_update;
  auto const deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (std::chrono::steady_clock::now() < deadline) {
    if (auto update = session.TryTakeUiUpdate()) {
      if (update->runtime_status.remote_presence.count(kUidB) > 0 &&
          update->runtime_status.remote_presence.at(kUidB) ==
              PeerPresence::kOnline) {
        status_update = std::move(update);
        break;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  CHECK(status_update.has_value());
  if (status_update->publication_bytes.has_value() &&
      !status_update->publication_bytes->empty()) {
    CHECK(status_update->publication_serial <= last_structural_serial);
  }

  session.RequestStop();
  session.Join();
  coordinator.RequestStop();
  coordinator.Join();
  std::filesystem::remove_all(state_dir);
}

}  // namespace

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::example::chat_demo::EnsureChatDemoModelRegistration();
  apptraverse::example::chat_demo::EnsureAetherLinkRegistration();

  TestSnapshotRevisionFrozenWhileUnread();
  std::cout << "  Snapshot revision frozen while unread passed\n";
  TestRemoteMessageUpdatesGuiMirror();
  std::cout << "  Remote message updates GUI mirror passed\n";
  TestIdlePollingDoesNotRewrite();
  std::cout << "  Idle polling does not rewrite passed\n";
  TestShutdownWithoutUiConsumption();
  std::cout << "  Shutdown without UI consumption passed\n";
  TestPresenceStatusWithoutGraphRewrite();
  std::cout << "  Presence status without graph rewrite passed\n";
  std::cout << "chat_session_publication_test passed!\n";
  return 0;
}
