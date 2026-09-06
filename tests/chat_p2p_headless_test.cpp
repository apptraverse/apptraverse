// Real Aether P2P headless integration: Model Domain + ChatAetherRuntime +
// SharedRuntime only. No UiMirror, UI Domain, presenters, or HWND.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/shared_event_id.h"
#include "apptraverse/shared_frame_codec.h"

#include "aether_runtime.h"
#include "aether_shared_transport.h"
#include "chat_bootstrap.h"
#include "chat_commands.h"
#include "chat_events.h"
#include "chat_log.h"
#include "chat_model.h"
#include "chat_shared.h"
#include "connection_trace.h"

namespace {

using namespace apptraverse;
using namespace chat;

constexpr auto kTimeout = std::chrono::seconds{120};
constexpr std::size_t kExpectedJournalSize = 6;  // 2 joins + 2 pre + 2 post

#define FAIL_EXIT(msg)                                                         \
  do {                                                                         \
    std::cerr << "FAIL: " << (msg) << '\n';                                   \
    std::exit(1);                                                              \
  } while (0)

std::string JournalEventTypeName(EventRecord const& record) {
  if (!record.event.is_valid()) {
    return "unknown";
  }
  record.event.Load();
  if (dynamic_cast<ClientAddedEvent const*>(&*record.event) != nullptr) {
    return "join";
  }
  if (dynamic_cast<ChatMessageEvent const*>(&*record.event) != nullptr) {
    return "message";
  }
  return "unknown";
}

std::string JournalAuthorUid(EventRecord const& record) {
  if (!record.event.is_valid()) {
    return {};
  }
  record.event.Load();
  if (auto const* join = dynamic_cast<ClientAddedEvent const*>(&*record.event)) {
    if (!join->client.is_valid()) {
      return {};
    }
    return join->client->AetherUidText();
  }
  if (auto const* message =
          dynamic_cast<ChatMessageEvent const*>(&*record.event)) {
    if (!message->author.is_valid()) {
      return {};
    }
    return message->author->AetherUidText();
  }
  return {};
}

std::string JournalMessageText(EventRecord const& record) {
  if (!record.event.is_valid()) {
    return {};
  }
  record.event.Load();
  auto const* message = dynamic_cast<ChatMessageEvent const*>(&*record.event);
  if (message == nullptr || !message->text.is_valid()) {
    return {};
  }
  message->text.Load();
  return message->text->bytes;
}

bool JournalsConverged(ChatRoom const& left, ChatRoom const& right) {
  if (left.journal.size() != right.journal.size()) {
    return false;
  }
  for (std::size_t i = 0; i < left.journal.size(); ++i) {
    auto const& a = left.journal[i];
    auto const& b = right.journal[i];
    if (!a.HasSharedIdentity() || !b.HasSharedIdentity()) {
      return false;
    }
    if (!(a.identity == b.identity) || !(a.order == b.order)) {
      return false;
    }
    if (JournalEventTypeName(a) != JournalEventTypeName(b) ||
        JournalAuthorUid(a) != JournalAuthorUid(b) ||
        JournalMessageText(a) != JournalMessageText(b)) {
      return false;
    }
  }
  return true;
}

bool PeersIdle(ChatSharedBinding const& binding) {
  for (auto const& peer : binding.instance.peers) {
    if (peer.HasOutstanding()) {
      return false;
    }
  }
  return binding.instance.deferred.empty();
}

struct ModelSide {
  std::unique_ptr<DirectoryDomainStorage> storage;
  std::unique_ptr<ae::Domain> model_domain;
  ChatApplication::ptr application;
  ChatSharedBinding shared;
  ChatAetherRuntime aether;
  std::unique_ptr<AetherSharedTransport> transport;

  std::mutex mu;
  std::condition_variable cv;
  std::queue<std::function<void()>> work;
  std::string uid;
  bool channel_ready{false};

  void Post(std::function<void()> fn) {
    {
      std::lock_guard<std::mutex> lock{mu};
      work.push(std::move(fn));
    }
    cv.notify_all();
  }

  void Drain() {
    for (;;) {
      std::function<void()> fn;
      {
        std::lock_guard<std::mutex> lock{mu};
        if (work.empty()) {
          return;
        }
        fn = std::move(work.front());
        work.pop();
      }
      fn();
    }
  }

  void EnsureRemotePeer(std::string const& remote_uid) {
    if (remote_uid.empty() || remote_uid == shared.instance.local_aether_uid) {
      return;
    }
    EnsureSharedPeer(shared, remote_uid);
  }

  void HandlePeerFrame(std::string remote_uid,
                       std::vector<std::uint8_t> bytes) {
    SharedEventFrame event_frame;
    if (DecodeSharedEventFrame(bytes, event_frame)) {
      auto const apply = ApplyIncomingSharedEvent(
          shared, remote_uid, event_frame,
          [this](std::string const& client_uid) {
            EnsureRemotePeer(client_uid);
          },
          [](ChatClient&) {});
      if (SharedApplyResultAllowsAck(apply)) {
        SendSharedAck(shared, transport.get(), remote_uid,
                      event_frame.event_id);
      }
      TickSharedDelivery(shared, std::chrono::steady_clock::now(),
                         transport.get());
      return;
    }
    SharedAckFrame ack_frame;
    if (DecodeSharedAckFrame(bytes, ack_frame)) {
      HandleSharedAck(shared, remote_uid, ack_frame);
      TickSharedDelivery(shared, std::chrono::steady_clock::now(),
                         transport.get());
    }
  }

  void OnPeerReady(std::string remote_uid) {
    auto* peer = shared.instance.FindPeer(remote_uid);
    bool const was_ready = peer != nullptr && peer->channel_ready;
    SetSharedPeerChannelReady(shared, remote_uid, true);
    channel_ready = true;
    EnsureRemotePeer(remote_uid);
    if (!was_ready) {
      TickSharedDelivery(shared, std::chrono::steady_clock::now(),
                         transport.get());
    }
  }

  void OnPeerClosed(std::string remote_uid) {
    SetSharedPeerChannelReady(shared, remote_uid, false);
    channel_ready = false;
  }

  void OnPeerWriteFailed(std::string remote_uid) {
    (void)remote_uid;
  }

  void Tick() {
    TickSharedDelivery(shared, std::chrono::steady_clock::now(),
                       transport.get());
  }
};

void LoadModelOnly(ModelSide& side, std::filesystem::path const& dir) {
  using chat::ChatObjId;
  using chat::ToObjId;
  side.storage = std::make_unique<DirectoryDomainStorage>(dir);
  side.model_domain =
      std::make_unique<ae::Domain>(*side.storage);
  side.application = LoadApplication<ChatApplication>(
      *side.model_domain, ae::ObjId{ToObjId(ChatObjId::Application)});
  side.transport = std::make_unique<AetherSharedTransport>(side.aether);
}

void WireAetherCallbacks(ModelSide& side) {
  side.aether.SetPeerCallbacks(
      [&side](std::string remote_uid) {
        side.Post([&, remote_uid = std::move(remote_uid)] {
          side.OnPeerReady(std::move(remote_uid));
        });
      },
      [&side](std::string remote_uid) {
        side.Post([&, remote_uid = std::move(remote_uid)] {
          side.OnPeerClosed(std::move(remote_uid));
        });
      },
      [&side](std::string remote_uid, std::vector<std::uint8_t> bytes) {
        side.Post([&, remote_uid = std::move(remote_uid),
                   bytes = std::move(bytes)]() mutable {
          side.HandlePeerFrame(std::move(remote_uid), std::move(bytes));
        });
      });
  side.aether.SetPeerWriteFailedCallback([&side](std::string remote_uid) {
    side.Post([&, remote_uid = std::move(remote_uid)] {
      side.OnPeerWriteFailed(std::move(remote_uid));
    });
  });
}

void StartAether(ModelSide& side, std::filesystem::path const& aether_dir) {
  WireAetherCallbacks(side);
  side.aether.Start(
      aether_dir,
      [&side](std::string uid_text) {
        side.Post([&, uid_text = std::move(uid_text)] {
          CreateUnjoinedLocalClient(*side.application, uid_text);
          side.uid = std::move(uid_text);
          InitializeChatSharedBinding(side.shared, *side.application,
                                      side.uid);
          std::cout << "UID ready: " << side.uid << '\n';
        });
      },
      [&side](PresenceState state) {
        // Diagnose on Aether thread; apply LocalPresenceEvent on Model thread.
        side.Post([&, state] {
          static_cast<void>(
              CommitPresenceChanged(*side.application->local_client, state));
          static_cast<void>(side.shared.presence.SetLocalSelf(state));
        });
      });
}

bool WaitUntil(std::chrono::steady_clock::time_point deadline,
               ModelSide& host, ModelSide& client,
               std::function<bool()> const& pred) {
  while (std::chrono::steady_clock::now() < deadline) {
    host.Drain();
    client.Drain();
    host.Tick();
    client.Tick();
    if (pred()) {
      return true;
    }
    std::unique_lock<std::mutex> lock{host.mu};
    host.cv.wait_for(lock, std::chrono::milliseconds{20});
  }
  host.Drain();
  client.Drain();
  return pred();
}

void ShutdownSide(ModelSide& side) {
  side.aether.RequestStop();
  side.aether.Join();
  side.Drain();
  if (side.application.is_valid()) {
    (void)side.application;
  }
}

}  // namespace

int main() {
  EnsureChatRegistration();

  auto const root =
      std::filesystem::temp_directory_path() /
      ("apptraverse_p2p_headless_" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  auto const host_dir = root / "host";
  auto const client_dir = root / "client";
  std::filesystem::create_directories(root);

  std::cout << "P2P headless state root: " << root.string() << '\n';

  DistillChatModel(host_dir, "Host");
  DistillChatModel(client_dir, "Client");

  ModelSide host;
  ModelSide client;
  LoadModelOnly(host, host_dir);
  LoadModelOnly(client, client_dir);
  SetApplicationRole(*host.application, ChatRole::Host);
  SetApplicationRole(*client.application, ChatRole::Client);

  SetChatLogPath((root / "p2p_headless.log").string());

  ConnectionTrace trace;
  trace.Mark("start");

  auto const deadline = std::chrono::steady_clock::now() + kTimeout;

  StartAether(host, host_dir / "aether");
  StartAether(client, client_dir / "aether");

  if (!WaitUntil(deadline, host, client, [&] {
        return !host.uid.empty() && !client.uid.empty();
      })) {
    ShutdownSide(host);
    ShutdownSide(client);
    FAIL_EXIT("timeout waiting for Aether UIDs");
  }
  trace.Mark("uids_ready");
  std::cout << "host_uid=" << host.uid << '\n';
  std::cout << "client_uid=" << client.uid << '\n';

  // Joins + optional pre-connect local messages (while still disconnected).
  CommitLocalJoin(host.shared, *host.application->local_client);
  CommitLocalMessage(host.shared, *host.application->local_client,
                     "host-pre-connect");
  CommitLocalJoin(client.shared, *client.application->local_client);
  CommitLocalMessage(client.shared, *client.application->local_client,
                     "client-pre-connect");
  host.aether.EnableLocalPresenceMonitoring();
  client.aether.EnableLocalPresenceMonitoring();
  trace.Mark("local_joins_and_pre_messages");

  ConnectToHostCommand(client.shared, host.uid,
                       [&](std::string const& peer_uid) {
                         client.EnsureRemotePeer(peer_uid);
                         client.aether.OpenPeer(peer_uid);
                       });
  trace.Mark("connect");

  if (!WaitUntil(deadline, host, client, [&] {
        return host.channel_ready && client.channel_ready;
      })) {
    trace.Dump(std::cerr);
    ShutdownSide(host);
    ShutdownSide(client);
    FAIL_EXIT("timeout waiting for P2P channel ready");
  }
  trace.Mark("stream_ready");

  // Wait until pre-connect journals converge (4 events: 2 joins + 2 msgs).
  if (!WaitUntil(deadline, host, client, [&] {
        return host.application->room->journal.size() >= 4 &&
               client.application->room->journal.size() >= 4 &&
               JournalsConverged(*host.application->room,
                                 *client.application->room);
      })) {
    trace.Dump(std::cerr);
    ShutdownSide(host);
    ShutdownSide(client);
    FAIL_EXIT("timeout waiting for pre-connect journal convergence");
  }
  trace.Mark("pre_converge");

  CommitLocalMessage(host.shared, *host.application->local_client,
                     "host-post-connect");
  CommitLocalMessage(client.shared, *client.application->local_client,
                     "client-post-connect");
  host.Tick();
  client.Tick();
  trace.Mark("post_messages");

  if (!WaitUntil(deadline, host, client, [&] {
        return host.application->room->journal.size() ==
                   kExpectedJournalSize &&
               client.application->room->journal.size() ==
                   kExpectedJournalSize &&
               JournalsConverged(*host.application->room,
                                 *client.application->room) &&
               PeersIdle(host.shared) && PeersIdle(client.shared);
      })) {
    for (auto const& peer : host.shared.instance.peers) {
      std::cerr << "host peer pending=" << peer.pending.size()
                << " in_flight=" << peer.in_flight.size()
                << " channel_ready=" << peer.channel_ready << '\n';
    }
    for (auto const& peer : client.shared.instance.peers) {
      std::cerr << "client peer pending=" << peer.pending.size()
                << " in_flight=" << peer.in_flight.size()
                << " channel_ready=" << peer.channel_ready << '\n';
    }
    std::cerr << "host journal size="
              << host.application->room->journal.size()
              << " client journal size="
              << client.application->room->journal.size()
              << " host_peers_idle=" << PeersIdle(host.shared)
              << " client_peers_idle=" << PeersIdle(client.shared)
              << " journals_match="
              << JournalsConverged(*host.application->room,
                                   *client.application->room)
              << '\n';
    trace.Dump(std::cerr);
    ShutdownSide(host);
    ShutdownSide(client);
    FAIL_EXIT("timeout waiting for full journal convergence + idle pending");
  }
  trace.Mark("converged");

  auto const connect_to_converge_ms = trace.ElapsedMs("connect", "converged");
  trace.Dump(std::cout);
  if (connect_to_converge_ms >= 0) {
    std::cout << "Connect→convergence: " << connect_to_converge_ms << " ms\n";
  }

  std::cout << "PASS criteria:\n";
  std::cout << "  - both Aether UIDs assigned\n";
  std::cout << "  - P2P stream ready both sides\n";
  std::cout << "  - journals converged (exact SharedEventId/Order/type/author/"
               "text), size="
            << kExpectedJournalSize << '\n';
  std::cout << "  - pending/in_flight/deferred empty both sides\n";
  std::cout << "PASS\n";

  ShutdownSide(host);
  ShutdownSide(client);

  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  return 0;
}
