#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "apptraverse/object_serialization.h"
#include "chat_command_limits.h"
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
using apptraverse::example::chat_demo::ChatUiUpdate;
using apptraverse::example::chat_demo::ChatWorkspace;
using apptraverse::example::chat_demo::DraftCommandOutcome;
using apptraverse::example::chat_demo::kMaxDraftTextBytes;
using apptraverse::example::chat_demo::kMaxPeerAdminIdBytes;
using apptraverse::example::chat_demo::OpenPeerRequest;
using apptraverse::example::chat_demo::SessionLifecycleState;
using apptraverse::example::chat_demo::test::FakeAetherFrameEndpoint;
using apptraverse::example::chat_demo::test::FakeEndpointCoordinator;

constexpr char const* kUidA = "a1111111-1111-4111-8111-111111111111";

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

void TestOversizedOpenPeerRejected() {
  apptraverse::MemoryNetwork network;
  FakeEndpointCoordinator coordinator{network};
  coordinator.Start();

  auto fake_slot = std::make_shared<FakeAetherFrameEndpoint*>(nullptr);
  auto state_dir = MakeTempDir("cmd_limits_open");
  ChatSession session([&coordinator, fake_slot, slot = fake_slot]() mutable {
    auto fake = std::make_unique<FakeAetherFrameEndpoint>(coordinator, kUidA);
    *slot = fake.get();
    return fake;
  });

  CHECK(session.Start(ChatSessionConfig{.state_dir = state_dir}, [] {}));
  WaitFake(fake_slot);
  WaitLifecycle(session, SessionLifecycleState::kReady);

  std::string huge_admin(kMaxPeerAdminIdBytes + 1, 'x');
  session.OpenPeer(OpenPeerRequest{.peer_admin_id = huge_admin});
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  CHECK(session.GetRuntimeStatus().error_text == "Open peer request too large");

  session.RequestStop();
  session.Join();
  coordinator.RequestStop();
  coordinator.Join();
  std::filesystem::remove_all(state_dir);
}

void TestOversizedDraftPreservesExisting() {
  apptraverse::MemoryNetwork network;
  FakeEndpointCoordinator coordinator{network};
  coordinator.Start();

  auto fake_slot = std::make_shared<FakeAetherFrameEndpoint*>(nullptr);
  auto state_dir = MakeTempDir("cmd_limits_draft");
  UiMirror ui;
  ChatSession session([&coordinator, fake_slot, slot = fake_slot]() mutable {
    auto fake = std::make_unique<FakeAetherFrameEndpoint>(coordinator, kUidA);
    *slot = fake.get();
    return fake;
  });

  CHECK(session.Start(ChatSessionConfig{.state_dir = state_dir}, [] {}));
  WaitFake(fake_slot);
  WaitLifecycle(session, SessionLifecycleState::kReady);

  session.OpenPeer(OpenPeerRequest{.peer_admin_id = "peer-a"});
  for (int i = 0; i < 100; ++i) {
    if (auto update = session.TryTakeUiUpdate()) {
      ApplyUiUpdate(ui, *update);
      if (ui.workspace.is_valid() && !ui.workspace->chats.empty()) {
        break;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  CHECK(ui.workspace.is_valid());
  CHECK(!ui.workspace->chats.empty());
  ae::ObjId const entry_id = ui.workspace->chats.front().id();

  session.EditDraft(entry_id, "keep-me", 1);
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  while (auto update = session.TryTakeUiUpdate()) {
    ApplyUiUpdate(ui, *update);
  }
  CHECK(ui.workspace->chats.front()->draft == "keep-me");

  std::string huge_draft(kMaxDraftTextBytes + 1, 'y');
  session.EditDraft(entry_id, huge_draft, 2);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  CHECK(session.GetRuntimeStatus().error_text == "Draft too large");
  {
    auto status = session.GetRuntimeStatus();
    auto it = status.latest_edit_result_by_entry.find(entry_id);
    CHECK(it != status.latest_edit_result_by_entry.end());
    CHECK(it->second.outcome == DraftCommandOutcome::kRejected);
    CHECK(it->second.revision == 2);
    CHECK(it->second.failure_reason == "Draft too large");
  }
  while (auto update = session.TryTakeUiUpdate()) {
    ApplyUiUpdate(ui, *update);
  }
  CHECK(ui.workspace->chats.front()->draft == "keep-me");

  // Empty string remains a valid local edit.
  session.EditDraft(entry_id, "", 3);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  {
    auto status = session.GetRuntimeStatus();
    auto it = status.latest_edit_result_by_entry.find(entry_id);
    CHECK(it != status.latest_edit_result_by_entry.end());
    CHECK(it->second.outcome == DraftCommandOutcome::kAccepted);
    CHECK(it->second.revision == 3);
  }
  while (auto update = session.TryTakeUiUpdate()) {
    ApplyUiUpdate(ui, *update);
  }
  CHECK(ui.workspace->chats.front()->draft.empty());

  session.RequestStop();
  session.Join();
  coordinator.RequestStop();
  coordinator.Join();
  std::filesystem::remove_all(state_dir);
}

}  // namespace

int main() {
  TestOversizedOpenPeerRejected();
  TestOversizedDraftPreservesExisting();
  std::cout << "chat_session_command_limits_test passed!\n";
  return 0;
}
