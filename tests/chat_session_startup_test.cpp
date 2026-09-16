#include <atomic>
#include <chrono>
#include <optional>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
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

using apptraverse::example::chat_demo::ChatSession;
using apptraverse::example::chat_demo::ChatSessionConfig;
using apptraverse::example::chat_demo::DemoRole;
using apptraverse::example::chat_demo::ChatWorkspace;
using apptraverse::example::chat_demo::IAetherFrameEndpoint;
using apptraverse::example::chat_demo::SessionLifecycleState;
using apptraverse::example::chat_demo::test::FakeAetherFrameEndpoint;
using apptraverse::example::chat_demo::test::FakeEndpointCoordinator;

// Syntactically valid canonical Aether UIDs (not placeholder strings).
constexpr char const* kLocalUid = "a1111111-1111-4111-8111-111111111111";

void TestChatSessionStartupShutdownWithFakeEndpoint() {
  auto const state_dir =
      std::filesystem::temp_directory_path() /
      ("chat_session_startup_" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::remove_all(state_dir);
  std::filesystem::create_directories(state_dir);

  apptraverse::MemoryNetwork network;
  FakeEndpointCoordinator coordinator{network};
  coordinator.Start();

  FakeAetherFrameEndpoint* fake_raw = nullptr;
  ChatSession session([&]() -> std::unique_ptr<IAetherFrameEndpoint> {
    auto fake =
        std::make_unique<FakeAetherFrameEndpoint>(coordinator, kLocalUid);
    fake->SetDeferReady(true);
    fake_raw = fake.get();
    return fake;
  });

  std::atomic<int> notify_count{0};
  CHECK(session.Start(ChatSessionConfig{.state_dir = state_dir, .role = DemoRole::kHost},
                      [&] { notify_count.fetch_add(1); }));

  // Initial model publication must be visible before network readiness.
  std::optional<apptraverse::example::chat_demo::ChatUiUpdate> initial_update;
  auto const deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline) {
    initial_update = session.TryTakeUiUpdate();
    if (initial_update.has_value() && initial_update->publication_bytes.has_value() &&
        !initial_update->publication_bytes->empty()) {
      break;
    }
    initial_update.reset();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  CHECK(initial_update.has_value());
  CHECK(initial_update->publication_bytes.has_value());
  auto const& pub_bytes = *initial_update->publication_bytes;
  CHECK(!pub_bytes.empty());
  CHECK(initial_update->kind ==
        apptraverse::example::chat_demo::ChatPublicationKind::kInitial);
  CHECK(initial_update->publication_serial >= 1);

  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  apptraverse::EnsureObjectRegistration();
  apptraverse::example::chat_demo::EnsureChatDemoModelRegistration();
  apptraverse::ByteSource in;
  in.data = pub_bytes.data();
  in.size = pub_bytes.size();
  auto root =
      apptraverse::LoadInitialPublication(in, ui_domain, ui_storage);
  CHECK(root);
  CHECK(root->GetClassId() == ChatWorkspace::kClassId);
  auto held = ui_domain.Find(root->obj_id);
  CHECK(held);
  auto workspace =
      ChatWorkspace::ptr{&ui_domain, root->obj_id, {}, std::move(held)};
  CHECK(workspace);

  // Network readiness still deferred — lifecycle should not be Ready yet.
  auto status = session.GetRuntimeStatus();
  CHECK(status.lifecycle_state != SessionLifecycleState::kReady);

  CHECK(fake_raw != nullptr);
  std::thread network_thread([fake_raw] { fake_raw->SignalReady(); });
  network_thread.join();

  auto const ready_deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < ready_deadline) {
    status = session.GetRuntimeStatus();
    if (status.lifecycle_state == SessionLifecycleState::kReady &&
        status.local_endpoint_uid == kLocalUid) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  CHECK(status.lifecycle_state == SessionLifecycleState::kReady);
  CHECK(status.local_endpoint_uid == kLocalUid);

  session.RequestStop();
  session.Join();
  status = session.GetRuntimeStatus();
  CHECK(status.lifecycle_state == SessionLifecycleState::kStopped);

  coordinator.RequestStop();
  coordinator.Join();
  std::filesystem::remove_all(state_dir);
}

}  // namespace

int main() {
  TestChatSessionStartupShutdownWithFakeEndpoint();
  std::cout << "chat_session_startup_test passed!\n";
  return 0;
}
