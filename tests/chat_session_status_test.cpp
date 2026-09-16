#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "chat_connectivity.h"
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

using apptraverse::example::chat_demo::ChatSession;
using apptraverse::example::chat_demo::ChatSessionConfig;
using apptraverse::example::chat_demo::IAetherFrameEndpoint;
using apptraverse::example::chat_demo::LocalConnectivityState;
using apptraverse::example::chat_demo::MessageDeliveryState;
using apptraverse::example::chat_demo::OpenPeerRequest;
using apptraverse::example::chat_demo::PeerPresence;
using apptraverse::example::chat_demo::SessionLifecycleState;
using apptraverse::example::chat_demo::test::FakeAetherFrameEndpoint;
using apptraverse::example::chat_demo::test::FakeEndpointCoordinator;

constexpr char const* kLocalUid = "a1111111-1111-4111-8111-111111111111";
constexpr char const* kRemoteUid = "b2222222-2222-4222-8222-222222222222";

std::filesystem::path MakeTempDir(char const* label) {
  auto const dir = std::filesystem::temp_directory_path() /
                   (std::string(label) + "_" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  return dir;
}

void WaitUntilEndpointStarted(ChatSession& session, char const* expected_uid) {
  auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline) {
    if (session.GetRuntimeStatus().local_endpoint_uid == expected_uid) {
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  CHECK(false && "endpoint start timeout");
}

void WaitUntilReady(ChatSession& session) {
  auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline) {
    if (session.GetRuntimeStatus().lifecycle_state == SessionLifecycleState::kReady) {
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  CHECK(false && "session ready timeout");
}

void TestLocalConnectivityNotFabricatedBeforeDiag() {
  apptraverse::MemoryNetwork network;
  FakeEndpointCoordinator coordinator{network};
  coordinator.Start();
  FakeAetherFrameEndpoint* fake = nullptr;
  ChatSession session([&]() -> std::unique_ptr<IAetherFrameEndpoint> {
    auto endpoint =
        std::make_unique<FakeAetherFrameEndpoint>(coordinator, kLocalUid);
    endpoint->SetDeferReady(true);
    fake = endpoint.get();
    return endpoint;
  });

  auto const state_dir = MakeTempDir("chat_session_status_local");
  CHECK(session.Start(ChatSessionConfig{.state_dir = state_dir}, [] {}));
  WaitUntilEndpointStarted(session, kLocalUid);
  fake->SignalReady();
  WaitUntilReady(session);

  auto const before = session.GetRuntimeStatus();
  CHECK(before.local_connectivity == LocalConnectivityState::kUnknown);

  fake->InjectLocalConnectivity(true, true);
  auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (std::chrono::steady_clock::now() < deadline) {
    if (session.GetRuntimeStatus().local_connectivity ==
        LocalConnectivityState::kOnline) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  CHECK(session.GetRuntimeStatus().local_connectivity ==
        LocalConnectivityState::kOnline);

  fake->InjectLocalConnectivity(true, false);
  auto const offline_deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (std::chrono::steady_clock::now() < offline_deadline) {
    if (session.GetRuntimeStatus().local_connectivity ==
        LocalConnectivityState::kOffline) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  CHECK(session.GetRuntimeStatus().local_connectivity ==
        LocalConnectivityState::kOffline);

  session.RequestStop();
  session.Join();
  std::filesystem::remove_all(state_dir);
}

void TestOfflineDraftSendAndReconnectPresence() {
  apptraverse::MemoryNetwork network;
  FakeEndpointCoordinator coordinator{network};
  coordinator.Start();

  FakeAetherFrameEndpoint* local_fake = nullptr;
  ChatSession local([&]() -> std::unique_ptr<IAetherFrameEndpoint> {
    auto endpoint =
        std::make_unique<FakeAetherFrameEndpoint>(coordinator, kLocalUid);
    endpoint->SetDeferReady(true);
    local_fake = endpoint.get();
    return endpoint;
  });

  FakeAetherFrameEndpoint* remote_fake = nullptr;
  ChatSession remote([&]() -> std::unique_ptr<IAetherFrameEndpoint> {
    auto endpoint =
        std::make_unique<FakeAetherFrameEndpoint>(coordinator, kRemoteUid);
    endpoint->SetDeferReady(true);
    remote_fake = endpoint.get();
    return endpoint;
  });

  auto const local_dir = MakeTempDir("chat_session_status_local_peer");
  auto const remote_dir = MakeTempDir("chat_session_status_remote_peer");
  CHECK(local.Start(ChatSessionConfig{.state_dir = local_dir}, [] {}));
  CHECK(remote.Start(ChatSessionConfig{.state_dir = remote_dir}, [] {}));
  WaitUntilEndpointStarted(local, kLocalUid);
  WaitUntilEndpointStarted(remote, kRemoteUid);
  local_fake->SignalReady();
  remote_fake->SignalReady();
  WaitUntilReady(local);
  WaitUntilReady(remote);

  local_fake->InjectLocalConnectivity(true, true);
  remote_fake->InjectLocalConnectivity(true, true);

  local.OpenPeer(OpenPeerRequest{.peer_admin_id = "status-peer",
                                 .peer_aether_uid = kRemoteUid});
  remote.OpenPeer(OpenPeerRequest{.peer_admin_id = "status-peer",
                                  .peer_aether_uid = kLocalUid});

  local_fake->InjectPresence(kRemoteUid, PeerPresence::kOffline);
  {
    auto const deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
      auto const status = local.GetRuntimeStatus();
      auto const it = status.remote_presence.find(kRemoteUid);
      if (it != status.remote_presence.end() &&
          it->second == PeerPresence::kOffline) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
  CHECK(local.GetRuntimeStatus().remote_presence.at(kRemoteUid) ==
        PeerPresence::kOffline);

  local_fake->InjectPresence(kRemoteUid, PeerPresence::kOnline);
  {
    auto const deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
      if (local.GetRuntimeStatus().remote_presence.at(kRemoteUid) ==
          PeerPresence::kOnline) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
  CHECK(local.GetRuntimeStatus().remote_presence.at(kRemoteUid) ==
        PeerPresence::kOnline);

  local.RequestStop();
  remote.RequestStop();
  local.Join();
  remote.Join();
  std::filesystem::remove_all(local_dir);
  std::filesystem::remove_all(remote_dir);
}

}  // namespace

int main() {
  std::cout << "Running apptraverse_chat_session_status_test...\n";
  TestLocalConnectivityNotFabricatedBeforeDiag();
  TestOfflineDraftSendAndReconnectPresence();
  std::cout << "All chat session status tests passed!\n";
  return 0;
}
