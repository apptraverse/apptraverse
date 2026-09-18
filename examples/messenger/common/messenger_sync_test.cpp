#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "aether-objects/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/memory_transport.h"
#include "apptraverse/node.h"
#include "apptraverse/runtime_node.h"

#include "aether_byte_transport.h"
#include "fake_aether_frame_endpoint.h"
#include "messenger_distill.h"
#include "messenger_ids.h"
#include "messenger_lifecycle.h"
#include "messenger_model.h"

namespace {

using apptraverse::example::chat_demo::IAetherFrameEndpoint;
using apptraverse::example::chat_demo::test::FakeAetherFrameEndpoint;
using apptraverse::example::chat_demo::test::FakeEndpointCoordinator;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"     \
                << __LINE__ << '\n';                                        \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

// Lexicographically smaller / larger Aether UIDs.
constexpr char const* kUidSmall = "a1111111-1111-4111-8111-111111111111";
constexpr char const* kUidLarge = "b2222222-2222-4222-8222-222222222222";

std::filesystem::path MakeTempDir(char const* tag) {
  auto path = std::filesystem::temp_directory_path() /
              (std::string("msg_sync_") + tag + "_" +
               std::to_string(
                   std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path);
  return path;
}

struct WorkQueue {
  std::mutex mu;
  std::vector<std::function<void()>> tasks;

  void Post(std::function<void()> task) {
    std::lock_guard<std::mutex> lock{mu};
    tasks.push_back(std::move(task));
  }

  void Drain() {
    for (;;) {
      std::vector<std::function<void()>> batch;
      {
        std::lock_guard<std::mutex> lock{mu};
        if (tasks.empty()) {
          return;
        }
        batch.swap(tasks);
      }
      for (auto& task : batch) {
        task();
      }
    }
  }
};

struct Side {
  std::filesystem::path state_dir;
  std::unique_ptr<apptraverse::DirectoryDomainStorage> storage;
  std::unique_ptr<ae::Domain> domain;
  apptraverse::Application::ptr app;
  std::unique_ptr<FakeAetherFrameEndpoint> aether;
  std::unique_ptr<apptraverse::example::chat_demo::AetherByteTransport>
      transport;
  std::unique_ptr<apptraverse::SharedSyncRuntime> sync;
  WorkQueue queue;
  apptraverse::PendingDirtyNodes dirty;

  apptraverse::Dialog& dialog() {
    return *app->surfaces->surfaces.front()->dialog;
  }
};

void BringUp(Side& side, FakeEndpointCoordinator& coordinator,
             std::string const& uid) {
  side.storage =
      std::make_unique<apptraverse::DirectoryDomainStorage>(side.state_dir);
  side.domain = std::make_unique<ae::Domain>(*side.storage);
  {
    auto distilled = apptraverse::BuildMessengerGraph(*side.domain);
    apptraverse::FinalizeDistilledGraph(*distilled);
    apptraverse::SaveDistilledRoot(*distilled);
  }
  side.app = apptraverse::LoadApplication<apptraverse::Application>(
      *side.domain, ae::ObjId{apptraverse::messenger::ToObjId(
                        apptraverse::messenger::ObjId::Application)});
  apptraverse::BindReachableNodesMaterializedChangeNotifier(
      *side.app, &side.dirty, &apptraverse::PendingDirtyNodesNotify);

  side.aether = std::make_unique<FakeAetherFrameEndpoint>(coordinator, uid);
  side.app->aether = side.aether.get();

  IAetherFrameEndpoint::Config cfg;
  cfg.state_dir = side.state_dir / "aether";
  cfg.client_name = "messenger-sync-test";
  Side* self = &side;
  side.aether->Start(
      std::move(cfg),
      [self, uid](std::string) {
        self->queue.Post([self, uid] {
          self->app->OnAetherLocalUid(uid);
        });
      },
      [self]() {
        self->queue.Post([self] {
          self->app->OnAetherReady();
          apptraverse::WireMessengerSyncStack(
              *self->app, *self->domain, *self->storage, *self->aether,
              [self](apptraverse::example::chat_demo::ModelTask task) {
                self->queue.Post(std::move(task));
              },
              self->transport, self->sync);
          self->app->SetupActivePeerSync();
        });
      },
      [](std::string error) {
        std::cerr << "fake aether failed: " << error << '\n';
        std::exit(1);
      },
      {}, {}, {},
      [self](std::string source, std::vector<std::uint8_t> bytes) {
        self->queue.Post([self, source = std::move(source),
                          bytes = std::move(bytes)] {
          self->app->OnControlMessage(std::move(source), std::move(bytes));
        });
      });
}

void Pump(Side& a, Side& b, int rounds = 40) {
  for (int i = 0; i < rounds; ++i) {
    a.queue.Drain();
    b.queue.Drain();
    if (a.app->sync_runtime != nullptr) {
      a.app->DriveConversationSync();
    }
    if (b.app->sync_runtime != nullptr) {
      b.app->DriveConversationSync();
    }
    a.queue.Drain();
    b.queue.Drain();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
}

void WaitReady(Side& a, Side& b) {
  auto const deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline) {
    Pump(a, b, 2);
    if (a.app->aether_ready && b.app->aether_ready &&
        a.app->sync_runtime != nullptr && b.app->sync_runtime != nullptr &&
        !a.dialog().own_uid.empty() && !b.dialog().own_uid.empty()) {
      return;
    }
  }
  CHECK(false && "sides not ready");
}

void WaitConversation(Side& a, Side& b) {
  auto const deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(8);
  while (std::chrono::steady_clock::now() < deadline) {
    Pump(a, b, 3);
    if (a.dialog().conversation.is_valid() &&
        b.dialog().conversation.is_valid()) {
      return;
    }
  }
  CHECK(false && "conversation not bound on both sides");
}

void TestOneWay(char const* initiator_uid, char const* peer_uid,
                char const* tag) {
  apptraverse::MemoryNetwork network;
  FakeEndpointCoordinator coordinator{network};
  coordinator.Start();

  Side initiator;
  Side peer;
  initiator.state_dir = MakeTempDir((std::string(tag) + "_init").c_str());
  peer.state_dir = MakeTempDir((std::string(tag) + "_peer").c_str());

  BringUp(initiator, coordinator, initiator_uid);
  BringUp(peer, coordinator, peer_uid);
  WaitReady(initiator, peer);

  CHECK(peer.dialog().peer_uid.empty());
  initiator.app->ConfirmPeerUid(peer_uid);
  WaitConversation(initiator, peer);

  CHECK(initiator.dialog().peer_uid == peer.dialog().own_uid);
  CHECK(peer.dialog().peer_uid == initiator.dialog().own_uid);
  CHECK(initiator.dialog().conversation.is_valid());
  CHECK(peer.dialog().conversation.is_valid());
  CHECK(initiator.dialog().conversation.id() ==
        peer.dialog().conversation.id());
  CHECK(initiator.dialog().conversation->HasMaterializedChangeNotifier());
  CHECK(peer.dialog().conversation->HasMaterializedChangeNotifier());

  initiator.dialog().SetDraft("hello-" + std::string(tag));
  initiator.app->AppendOutgoingMessage("hello-" + std::string(tag));
  CHECK(initiator.dialog().draft.empty());

  auto const deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(8);
  while (std::chrono::steady_clock::now() < deadline) {
    Pump(initiator, peer, 3);
    if (peer.dialog().conversation.is_valid() &&
        !peer.dialog().conversation->messages.empty() &&
        peer.dialog().conversation->messages.back().text ==
            "hello-" + std::string(tag)) {
      break;
    }
  }
  CHECK(peer.dialog().conversation->messages.size() >= 1);
  CHECK(peer.dialog().conversation->messages.back().text ==
        "hello-" + std::string(tag));
  CHECK(peer.dialog().conversation->HasMaterializedChangeNotifier());

  peer.dialog().SetDraft("reply-" + std::string(tag));
  peer.app->AppendOutgoingMessage("reply-" + std::string(tag));
  while (std::chrono::steady_clock::now() <
         deadline + std::chrono::seconds(8)) {
    Pump(initiator, peer, 3);
    if (initiator.dialog().conversation->messages.size() >= 2) {
      break;
    }
  }
  CHECK(initiator.dialog().conversation->messages.size() >= 2);
  CHECK(initiator.dialog().conversation->messages.back().text ==
        "reply-" + std::string(tag));

  // Simultaneous re-add must not replace journal id.
  auto const conv_id = initiator.dialog().conversation.id();
  initiator.app->ConfirmPeerUid(peer_uid);
  peer.app->ConfirmPeerUid(initiator_uid);
  Pump(initiator, peer, 10);
  CHECK(initiator.dialog().conversation.id() == conv_id);
  CHECK(peer.dialog().conversation.id() == conv_id);

  initiator.aether->RequestStop();
  peer.aether->RequestStop();
  initiator.aether->Join();
  peer.aether->Join();
  coordinator.RequestStop();
  coordinator.Join();
  std::filesystem::remove_all(initiator.state_dir);
  std::filesystem::remove_all(peer.state_dir);
}

void TestRestartRegisters() {
  apptraverse::MemoryNetwork network;
  FakeEndpointCoordinator coordinator{network};
  coordinator.Start();

  auto state_a = MakeTempDir("restart_a");
  auto state_b = MakeTempDir("restart_b");

  {
    Side a;
    Side b;
    a.state_dir = state_a;
    b.state_dir = state_b;
    BringUp(a, coordinator, kUidSmall);
    BringUp(b, coordinator, kUidLarge);
    WaitReady(a, b);
    a.app->ConfirmPeerUid(kUidLarge);
    WaitConversation(a, b);
    a.app->AppendOutgoingMessage("before-restart");
    auto const deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < deadline) {
      Pump(a, b, 3);
      if (b.dialog().conversation.is_valid() &&
          !b.dialog().conversation->messages.empty()) {
        break;
      }
    }
    CHECK(!b.dialog().conversation->messages.empty());
    a.app.Save();
    b.app.Save();
    a.aether->RequestStop();
    b.aether->RequestStop();
    a.aether->Join();
    b.aether->Join();
  }

  Side a2;
  Side b2;
  a2.state_dir = state_a;
  b2.state_dir = state_b;
  // Reload without re-distill.
  a2.storage =
      std::make_unique<apptraverse::DirectoryDomainStorage>(a2.state_dir);
  a2.domain = std::make_unique<ae::Domain>(*a2.storage);
  a2.app = apptraverse::LoadApplication<apptraverse::Application>(
      *a2.domain, ae::ObjId{apptraverse::messenger::ToObjId(
                     apptraverse::messenger::ObjId::Application)});
  apptraverse::BindReachableNodesMaterializedChangeNotifier(
      *a2.app, &a2.dirty, &apptraverse::PendingDirtyNodesNotify);
  CHECK(a2.dialog().conversation.is_valid());
  auto const saved_id = a2.dialog().conversation.id();

  a2.aether = std::make_unique<FakeAetherFrameEndpoint>(coordinator, kUidSmall);
  a2.app->aether = a2.aether.get();
  IAetherFrameEndpoint::Config cfg;
  cfg.state_dir = a2.state_dir / "aether";
  cfg.client_name = "messenger-sync-test";
  Side* self = &a2;
  a2.aether->Start(
      std::move(cfg),
      [self](std::string uid) {
        self->queue.Post([self, uid = std::move(uid)] {
          self->app->OnAetherLocalUid(std::move(uid));
        });
      },
      [self]() {
        self->queue.Post([self] {
          self->app->OnAetherReady();
          apptraverse::WireMessengerSyncStack(
              *self->app, *self->domain, *self->storage, *self->aether,
              [self](apptraverse::example::chat_demo::ModelTask task) {
                self->queue.Post(std::move(task));
              },
              self->transport, self->sync);
          self->app->SetupActivePeerSync();
        });
      },
      [](std::string) {}, {}, {}, {},
      [self](std::string source, std::vector<std::uint8_t> bytes) {
        self->queue.Post([self, source = std::move(source),
                          bytes = std::move(bytes)] {
          self->app->OnControlMessage(std::move(source), std::move(bytes));
        });
      });

  auto const deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline) {
    a2.queue.Drain();
    if (a2.app->sync_runtime != nullptr) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  CHECK(a2.app->sync_runtime != nullptr);
  a2.app->SetupActivePeerSync();
  CHECK(a2.app->sync_runtime->FindNode(saved_id).is_valid());
  CHECK(a2.dialog().conversation.id() == saved_id);

  a2.aether->RequestStop();
  a2.aether->Join();
  coordinator.RequestStop();
  coordinator.Join();
  std::filesystem::remove_all(state_a);
  std::filesystem::remove_all(state_b);
}

}  // namespace

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureMessengerModelRegistration();

  TestOneWay(kUidSmall, kUidLarge, "small_adds");
  TestOneWay(kUidLarge, kUidSmall, "large_adds");
  TestRestartRegisters();

  std::cout << "messenger_sync_test ok\n";
  return 0;
}
