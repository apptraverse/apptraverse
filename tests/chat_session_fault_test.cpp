// ChatSession fault and safe-failure regression (Windows + fakes).
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj_id.h"

#include "apptraverse/memory_transport.h"
#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/runtime_node.h"
#include "chat_commands.h"
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

using apptraverse::example::chat_demo::BindLocalEndpoint;
using apptraverse::example::chat_demo::ChatPublicationKind;
using apptraverse::example::chat_demo::ChatSession;
using apptraverse::example::chat_demo::ChatSessionConfig;
using apptraverse::example::chat_demo::ChatUiUpdate;
using apptraverse::example::chat_demo::ChatWorkspace;
using apptraverse::example::chat_demo::OpenPeerRequest;
using apptraverse::example::chat_demo::PeerPresence;
using apptraverse::example::chat_demo::SessionLifecycleState;
using apptraverse::example::chat_demo::test::FakeAetherFrameEndpoint;
using apptraverse::example::chat_demo::test::FakeEndpointCoordinator;

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

void SeedPersistedLocalUid(std::filesystem::path const& state_dir,
                           std::string const& uid) {
  auto const model_dir = state_dir / "model";
  std::filesystem::create_directories(model_dir);
  apptraverse::DirectoryDomainStorage storage{model_dir};
  ae::Domain domain{storage};
  auto workspace = ChatWorkspace::ptr::Create(
      ae::CreateWith{domain}.with_id(kLocalWorkspaceRootId));
  apptraverse::InitializeRuntimeNode(*workspace);
  CHECK(BindLocalEndpoint(*workspace, uid, [] {}));
  workspace.Save();
}

struct UiMirror {
  std::unique_ptr<ae::RamDomainStorage> storage =
      std::make_unique<ae::RamDomainStorage>();
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
    ui.domain = std::make_unique<ae::Domain>(*ui.storage);
    auto root = apptraverse::LoadInitialPublication(in, *ui.domain, *ui.storage);
    CHECK(root);
    auto held = ui.domain->Find(root->obj_id);
    CHECK(held);
    ui.workspace = ChatWorkspace::ptr{ui.domain.get(), root->obj_id, {},
                                     std::move(held)};
  } else {
    apptraverse::ApplyStructuralPublicationAndUpdatePresenters(
        in, *ui.domain, *ui.storage, *ui.workspace);
  }
}

void DrainUi(ChatSession& session, UiMirror& ui) {
  while (auto update = session.TryTakeUiUpdate()) {
    ApplyUiUpdate(ui, *update);
  }
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

void WaitLifecycle(ChatSession& session, SessionLifecycleState expected,
                   std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (session.GetRuntimeStatus().lifecycle_state == expected) {
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  CHECK(false && "lifecycle state timeout");
}

void TestCallbackAfterHostDetach() {
  apptraverse::MemoryNetwork network;
  FakeEndpointCoordinator coordinator{network};
  coordinator.Start();

  auto fake_slot = std::make_shared<FakeAetherFrameEndpoint*>(nullptr);
  auto state_dir = MakeTempDir("fault_detach");
  {
    ChatSession session([&coordinator, fake_slot, slot = fake_slot]() mutable {
      auto fake = std::make_unique<FakeAetherFrameEndpoint>(coordinator, kUidA);
      *slot = fake.get();
      return fake;
    });
    CHECK(session.Start(ChatSessionConfig{.state_dir = state_dir}, [] {}));
    auto* fake = WaitFake(fake_slot);
    WaitLifecycle(session, SessionLifecycleState::kReady);
    fake->InjectPresence(kUidB, PeerPresence::kOnline);
    session.RequestStop();
    session.Join();
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  coordinator.RequestStop();
  coordinator.Join();
  std::filesystem::remove_all(state_dir);
}

void TestPendingPublicationWhileModelReceives() {
  apptraverse::MemoryNetwork network;
  FakeEndpointCoordinator coordinator{network};
  coordinator.Start();

  auto fake_a = std::make_shared<FakeAetherFrameEndpoint*>(nullptr);
  auto fake_b = std::make_shared<FakeAetherFrameEndpoint*>(nullptr);
  auto state_a = MakeTempDir("fault_pub_a");
  auto state_b = MakeTempDir("fault_pub_b");

  ChatSession local([&coordinator, fake_a, slot = fake_a]() mutable {
    auto fake = std::make_unique<FakeAetherFrameEndpoint>(coordinator, kUidA);
    *slot = fake.get();
    return fake;
  });
  ChatSession remote([&coordinator, fake_b, slot = fake_b]() mutable {
    auto fake = std::make_unique<FakeAetherFrameEndpoint>(coordinator, kUidB);
    *slot = fake.get();
    return fake;
  });

  UiMirror ui_local;
  UiMirror ui_remote;
  CHECK(local.Start(ChatSessionConfig{.state_dir = state_a}, [] {}));
  CHECK(remote.Start(ChatSessionConfig{.state_dir = state_b}, [] {}));
  WaitFake(fake_a);
  WaitFake(fake_b);
  WaitLifecycle(local, SessionLifecycleState::kReady);
  WaitLifecycle(remote, SessionLifecycleState::kReady);

  local.OpenPeer(
      OpenPeerRequest{.peer_admin_id = "peer", .peer_aether_uid = kUidB});
  remote.OpenPeer(
      OpenPeerRequest{.peer_admin_id = "peer", .peer_aether_uid = kUidA});
  std::this_thread::sleep_for(std::chrono::seconds(2));
  DrainUi(local, ui_local);
  DrainUi(remote, ui_remote);

  auto initial = local.TryTakeUiUpdate();
  if (initial.has_value()) {
    ApplyUiUpdate(ui_local, *initial);
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  DrainUi(local, ui_local);
  DrainUi(remote, ui_remote);

  local.RequestStop();
  remote.RequestStop();
  local.Join();
  remote.Join();
  coordinator.RequestStop();
  coordinator.Join();
  std::filesystem::remove_all(state_a);
  std::filesystem::remove_all(state_b);
}

void TestCloseDuringRegistration() {
  apptraverse::MemoryNetwork network;
  FakeEndpointCoordinator coordinator{network};
  coordinator.Start();

  auto fake_slot = std::make_shared<FakeAetherFrameEndpoint*>(nullptr);
  auto state_dir = MakeTempDir("fault_close_reg");
  ChatSession session([&coordinator, fake_slot, slot = fake_slot]() mutable {
    auto fake = std::make_unique<FakeAetherFrameEndpoint>(coordinator, kUidA);
    fake->SetDeferReady(true);
    *slot = fake.get();
    return fake;
  });

  CHECK(session.Start(ChatSessionConfig{.state_dir = state_dir}, [] {}));
  WaitFake(fake_slot);
  session.OpenPeer(
      OpenPeerRequest{.peer_admin_id = "bob", .peer_aether_uid = kUidB});
  session.RequestStop();
  session.Join();
  CHECK(session.GetRuntimeStatus().lifecycle_state ==
        SessionLifecycleState::kStopped);

  coordinator.RequestStop();
  coordinator.Join();
  std::filesystem::remove_all(state_dir);
}

void TestInvalidProfileLoad() {
  auto state_dir = MakeTempDir("fault_invalid_profile");
  std::filesystem::create_directories(state_dir / "model");
  std::ofstream corrupt{state_dir / "model" / "corrupt.bin"};
  corrupt << "not-a-domain";
  corrupt.close();

  apptraverse::MemoryNetwork network;
  FakeEndpointCoordinator coordinator{network};
  coordinator.Start();

  auto fake_slot = std::make_shared<FakeAetherFrameEndpoint*>(nullptr);
  ChatSession session([&coordinator, fake_slot, slot = fake_slot]() mutable {
    auto fake = std::make_unique<FakeAetherFrameEndpoint>(coordinator, kUidA);
    *slot = fake.get();
    return fake;
  });

  CHECK(session.Start(ChatSessionConfig{.state_dir = state_dir}, [] {}));
  WaitFake(fake_slot);
  // DirectoryDomainStorage ignores unknown files; worker should still reach
  // Ready or Failed without deleting the profile directory.
  auto const deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  SessionLifecycleState lifecycle = SessionLifecycleState::kStarting;
  while (std::chrono::steady_clock::now() < deadline) {
    lifecycle = session.GetRuntimeStatus().lifecycle_state;
    if (lifecycle == SessionLifecycleState::kReady ||
        lifecycle == SessionLifecycleState::kFailed ||
        lifecycle == SessionLifecycleState::kStopped) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  CHECK(lifecycle == SessionLifecycleState::kReady ||
        lifecycle == SessionLifecycleState::kFailed ||
        lifecycle == SessionLifecycleState::kStopped ||
        !session.GetRuntimeStatus().error_text.empty());
  CHECK(std::filesystem::exists(state_dir));

  session.RequestStop();
  session.Join();
  CHECK(session.IsFinished());
  coordinator.RequestStop();
  coordinator.Join();
  std::filesystem::remove_all(state_dir);
}

void TestIdentityConflictOnRestart() {
  apptraverse::MemoryNetwork network;
  FakeEndpointCoordinator coordinator{network};
  coordinator.Start();

  auto state_dir = MakeTempDir("fault_identity");
  SeedPersistedLocalUid(state_dir, kUidA);

  auto fake_slot = std::make_shared<FakeAetherFrameEndpoint*>(nullptr);
  ChatSession session([&coordinator, fake_slot, slot = fake_slot]() mutable {
    auto fake = std::make_unique<FakeAetherFrameEndpoint>(coordinator, kUidB);
    *slot = fake.get();
    return fake;
  });

  CHECK(session.Start(ChatSessionConfig{.state_dir = state_dir}, [] {}));
  WaitFake(fake_slot);
  WaitLifecycle(session, SessionLifecycleState::kFailed);
  CHECK(!session.GetRuntimeStatus().error_text.empty());

  session.RequestStop();
  session.Join();
  coordinator.RequestStop();
  coordinator.Join();
  std::filesystem::remove_all(state_dir);
}

void TestCheckpointReload() {
  apptraverse::MemoryNetwork network;
  FakeEndpointCoordinator coordinator{network};
  coordinator.Start();

  auto state_dir = MakeTempDir("fault_checkpoint");
  SeedPersistedLocalUid(state_dir, kUidA);

  {
    auto fake_slot = std::make_shared<FakeAetherFrameEndpoint*>(nullptr);
    ChatSession session([&coordinator, fake_slot, slot = fake_slot]() mutable {
      auto fake = std::make_unique<FakeAetherFrameEndpoint>(coordinator, kUidA);
      *slot = fake.get();
      return fake;
    });
    CHECK(session.Start(ChatSessionConfig{.state_dir = state_dir}, [] {}));
    WaitLifecycle(session, SessionLifecycleState::kReady);
    session.OpenPeer(OpenPeerRequest{.peer_admin_id = "saved-peer"});
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(session.Checkpoint(42));
    auto const cp_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < cp_deadline) {
      if (session.GetRuntimeStatus().completed_checkpoint_id == 42) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    CHECK(session.GetRuntimeStatus().completed_checkpoint_id == 42);
    session.RequestStop();
    session.Join();
    CHECK(session.IsFinished());
  }

  {
    auto fake_slot = std::make_shared<FakeAetherFrameEndpoint*>(nullptr);
    ChatSession session([&coordinator, fake_slot, slot = fake_slot]() mutable {
      auto fake = std::make_unique<FakeAetherFrameEndpoint>(coordinator, kUidA);
      *slot = fake.get();
      return fake;
    });
    UiMirror ui;
    CHECK(session.Start(ChatSessionConfig{.state_dir = state_dir}, [] {}));
    WaitLifecycle(session, SessionLifecycleState::kReady);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    DrainUi(session, ui);
    CHECK(ui.workspace.is_valid());
    CHECK(!ui.workspace->chats.empty());
    session.RequestStop();
    session.Join();
  }

  coordinator.RequestStop();
  coordinator.Join();
  std::filesystem::remove_all(state_dir);
}

void TestRetryAfterNetworkFailure() {
  apptraverse::MemoryNetwork network;
  FakeEndpointCoordinator coordinator{network};
  coordinator.Start();

  auto state_dir = MakeTempDir("fault_retry");
  SeedPersistedLocalUid(state_dir, kUidA);

  auto fake_slot = std::make_shared<FakeAetherFrameEndpoint*>(nullptr);
  ChatSession session([&coordinator, fake_slot]() {
    auto fake = std::make_unique<FakeAetherFrameEndpoint>(coordinator, kUidA);
    *fake_slot = fake.get();
    return fake;
  });

  CHECK(session.Start(ChatSessionConfig{.state_dir = state_dir}, [] {}));
  WaitFake(fake_slot);
  WaitLifecycle(session, SessionLifecycleState::kReady);

  (*fake_slot)->SignalFailed("simulated network failure");
  WaitLifecycle(session, SessionLifecycleState::kFailed);
  CHECK(session.IsFinished() == false);

  auto const t0 = std::chrono::steady_clock::now();
  session.RetryConnection();
  auto const retry_elapsed = std::chrono::steady_clock::now() - t0;
  CHECK(retry_elapsed < std::chrono::milliseconds(200));

  WaitLifecycle(session, SessionLifecycleState::kReady);
  CHECK(session.GetRuntimeStatus().error_text.empty());
  CHECK(session.IsFinished() == false);
  CHECK(std::filesystem::exists(state_dir));

  session.RequestStop();
  session.Join();
  CHECK(session.IsFinished());
  coordinator.RequestStop();
  coordinator.Join();
  std::filesystem::remove_all(state_dir);
}

}  // namespace

int main() {
  TestCallbackAfterHostDetach();
  TestPendingPublicationWhileModelReceives();
  TestCloseDuringRegistration();
  TestInvalidProfileLoad();
  TestIdentityConflictOnRestart();
  TestCheckpointReload();
  TestRetryAfterNetworkFailure();
  std::cout << "chat_session_fault_test passed!\n";
  return 0;
}
