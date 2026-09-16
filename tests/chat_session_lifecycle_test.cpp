#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether_link.h"
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
using apptraverse::example::chat_demo::ChatSession;
using apptraverse::example::chat_demo::ChatSessionConfig;
using apptraverse::example::chat_demo::ChatWorkspace;
using apptraverse::example::chat_demo::IAetherFrameEndpoint;
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

void WaitPresence(ChatSession& session, std::string const& peer,
                  PeerPresence expected,
                  std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    auto status = session.GetRuntimeStatus();
    auto it = status.remote_presence.find(peer);
    if (it != status.remote_presence.end() && it->second == expected) {
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  CHECK(false && "presence update timeout");
}

void TestPresenceInjectedOnModelThread() {
  apptraverse::MemoryNetwork network;
  FakeEndpointCoordinator coordinator{network};
  coordinator.Start();

  auto fake_slot = std::make_shared<FakeAetherFrameEndpoint*>(nullptr);
  auto state_dir = MakeTempDir("chat_lifecycle_presence");
  ChatSession session([&coordinator, fake_slot, slot = fake_slot]() mutable {
    auto fake = std::make_unique<FakeAetherFrameEndpoint>(coordinator, kUidA);
    *slot = fake.get();
    return fake;
  });

  CHECK(session.Start(ChatSessionConfig{.state_dir = state_dir}, [] {}));
  auto* fake = WaitFake(fake_slot);
  WaitLifecycle(session, SessionLifecycleState::kReady);

  for (int i = 0; i < 50; ++i) {
    fake->InjectPresence(kUidB, PeerPresence::kConnecting);
    fake->InjectPresence(kUidB, PeerPresence::kOnline);
  }
  WaitPresence(session, kUidB, PeerPresence::kOnline);

  session.RequestStop();
  session.Join();
  CHECK(session.GetRuntimeStatus().lifecycle_state ==
        SessionLifecycleState::kStopped);

  coordinator.RequestStop();
  coordinator.Join();
  std::filesystem::remove_all(state_dir);
}

void TestStopDuringRegistration() {
  apptraverse::MemoryNetwork network;
  FakeEndpointCoordinator coordinator{network};
  coordinator.Start();

  auto fake_slot = std::make_shared<FakeAetherFrameEndpoint*>(nullptr);
  auto state_dir = MakeTempDir("chat_lifecycle_stop_reg");
  ChatSession session([&coordinator, fake_slot, slot = fake_slot]() mutable {
    auto fake = std::make_unique<FakeAetherFrameEndpoint>(coordinator, kUidA);
    fake->SetDeferReady(true);
    *slot = fake.get();
    return fake;
  });

  CHECK(session.Start(ChatSessionConfig{.state_dir = state_dir}, [] {}));
  WaitFake(fake_slot);

  session.OpenPeer(OpenPeerRequest{
      .peer_admin_id = "bob",
      .peer_aether_uid = std::string{kUidB},
  });

  session.RequestStop();
  session.Join();
  CHECK(session.GetRuntimeStatus().lifecycle_state ==
        SessionLifecycleState::kStopped);

  coordinator.RequestStop();
  coordinator.Join();
  std::filesystem::remove_all(state_dir);
}

void TestStopWhileDraftQueued() {
  apptraverse::MemoryNetwork network;
  FakeEndpointCoordinator coordinator{network};
  coordinator.Start();

  auto fake_slot = std::make_shared<FakeAetherFrameEndpoint*>(nullptr);
  auto state_dir = MakeTempDir("chat_lifecycle_stop_draft");
  ChatSession session([&coordinator, fake_slot, slot = fake_slot]() mutable {
    auto fake = std::make_unique<FakeAetherFrameEndpoint>(coordinator, kUidA);
    *slot = fake.get();
    return fake;
  });

  CHECK(session.Start(ChatSessionConfig{.state_dir = state_dir}, [] {}));
  WaitFake(fake_slot);

  session.OpenPeer(OpenPeerRequest{
      .peer_admin_id = "bob",
      .peer_aether_uid = std::string{kUidB},
  });
  session.EditDraft(ae::ObjId{0}, "queued draft", 1);
  session.SaveBounds({.valid = true, .x = 10, .y = 20, .width = 800, .height = 600});

  session.RequestStop();
  session.Join();
  CHECK(session.GetRuntimeStatus().lifecycle_state ==
        SessionLifecycleState::kStopped);

  coordinator.RequestStop();
  coordinator.Join();
  std::filesystem::remove_all(state_dir);
}

void TestFailedStartupAllowsHostClose() {
  apptraverse::MemoryNetwork network;
  FakeEndpointCoordinator coordinator{network};
  coordinator.Start();

  auto fake_slot = std::make_shared<FakeAetherFrameEndpoint*>(nullptr);
  auto state_dir = MakeTempDir("chat_lifecycle_failed");
  ChatSession session([&coordinator, fake_slot, slot = fake_slot]() mutable {
    auto fake = std::make_unique<FakeAetherFrameEndpoint>(coordinator, kUidA);
    fake->SetDeferReady(true);
    *slot = fake.get();
    return fake;
  });

  CHECK(session.Start(ChatSessionConfig{.state_dir = state_dir}, [] {}));
  auto* fake = WaitFake(fake_slot);
  {
    auto const deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
      if (!session.GetRuntimeStatus().local_endpoint_uid.empty()) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(!session.GetRuntimeStatus().local_endpoint_uid.empty());
  }
  fake->SignalFailed("simulated startup failure");
  WaitLifecycle(session, SessionLifecycleState::kFailed);

  session.RequestStop();
  session.Join();
  CHECK(session.GetRuntimeStatus().lifecycle_state ==
        SessionLifecycleState::kFailed);

  coordinator.RequestStop();
  coordinator.Join();
  std::filesystem::remove_all(state_dir);
}

void TestEndpointUidMismatchFailsWithoutOverwrite() {
  apptraverse::MemoryNetwork network;
  FakeEndpointCoordinator coordinator{network};
  coordinator.Start();

  auto state_dir = MakeTempDir("chat_lifecycle_uid_mismatch");
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

  auto status = session.GetRuntimeStatus();
  CHECK(status.error_text.find("Local endpoint UID conflict") !=
        std::string::npos);
  CHECK(status.local_endpoint_uid.empty());

  session.RequestStop();
  session.Join();

  apptraverse::DirectoryDomainStorage storage{state_dir / "model"};
  ae::Domain domain{storage};
  auto workspace = ChatWorkspace::ptr::Declare(
      ae::CreateWith{domain}.with_id(kLocalWorkspaceRootId));
  workspace.Load();
  CHECK(workspace);
  CHECK(workspace->local_endpoint_uid == kUidA);

  coordinator.RequestStop();
  coordinator.Join();
  std::filesystem::remove_all(state_dir);
}

}  // namespace

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::example::chat_demo::EnsureChatDemoModelRegistration();
  apptraverse::example::chat_demo::EnsureAetherLinkRegistration();

  TestPresenceInjectedOnModelThread();
  std::cout << "  Presence on model thread passed\n";
  TestStopDuringRegistration();
  std::cout << "  Stop during registration passed\n";
  TestStopWhileDraftQueued();
  std::cout << "  Stop while drafts queued passed\n";
  TestFailedStartupAllowsHostClose();
  std::cout << "  Failed startup host close passed\n";
  TestEndpointUidMismatchFailsWithoutOverwrite();
  std::cout << "  Endpoint UID mismatch passed\n";
  std::cout << "chat_session_lifecycle_test passed!\n";
  return 0;
}
