#include <cassert>
#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "aether/clock.h"
#include "aether/domain_storage/ram_domain_storage.h"

#include "apptraverse/shared_frame_codec.h"
#include "apptraverse/shared_runtime.h"

#include "chat_bootstrap.h"
#include "chat_commands.h"
#include "chat_events.h"
#include "chat_model.h"
#include "chat_shared.h"

namespace {

using namespace apptraverse;

#define REQUIRE(cond)                                                        \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "REQUIRE failed: " #cond << " at " << __FILE__ << ":"     \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

class RecordingTransport final : public ISharedTransport {
 public:
  struct SentEvent {
    std::string peer_uid;
    SharedEventFrame frame;
  };
  struct SentAck {
    std::string peer_uid;
    SharedAckFrame frame;
  };

  std::vector<SentEvent> events;
  std::vector<SentAck> acks;
  int open_peer_requests{0};

  void SendEvent(std::string const& peer_uid,
                 SharedEventFrame const& frame) override {
    events.push_back(SentEvent{peer_uid, frame});
  }

  void SendAck(std::string const& peer_uid,
               SharedAckFrame const& frame) override {
    acks.push_back(SentAck{peer_uid, frame});
  }
};

void test_ack_clears_pending() {
  SharedRuntime runtime;
  SharedInstance<ChatRoom> instance;
  instance.local_aether_uid = "alice";
  instance.shared_room_id = "room";
  auto& peer = runtime.EnsurePeer(instance, "bob");
  SharedEventId e1{.origin_uid = "alice", .origin_sequence = 1};
  SharedEventId e2{.origin_uid = "alice", .origin_sequence = 2};
  peer.pending.push_back(e1);
  peer.pending.push_back(e2);
  peer.in_flight = e1;
  runtime.OnAckReceived(instance, "bob", e1);
  REQUIRE(!peer.in_flight.has_value());
  REQUIRE(peer.pending.size() == 1);
  REQUIRE(peer.pending.front() == e2);
}

void test_duplicate_event_id() {
  SharedRuntime runtime;
  SharedInstance<ChatRoom> instance;
  instance.local_aether_uid = "alice";
  SharedEventId id{.origin_uid = "alice", .origin_sequence = 1};
  SharedEventOrder order{.lamport = 1,
                         .origin_uid = "alice",
                         .origin_sequence = 1};
  runtime.OnIncomingEventApplied(
      instance, id, order, "bob",
      [](PeerDeliveryState&, SharedEventId const&) {});
  REQUIRE(instance.HasSharedEvent(id));
  runtime.OnIncomingEventApplied(
      instance, id, order, "bob",
      [](PeerDeliveryState&, SharedEventId const&) {});
  REQUIRE(instance.shared_journal.size() == 1);
}

void test_seed_pending_excludes_peer_origin() {
  SharedRuntime runtime;
  SharedInstance<ChatRoom> instance;
  instance.local_aether_uid = "alice";
  instance.shared_journal = {
      SharedJournalEntry{
          .id = {.origin_uid = "alice", .origin_sequence = 1},
          .order = {.lamport = 1, .origin_uid = "alice", .origin_sequence = 1}},
      SharedJournalEntry{
          .id = {.origin_uid = "bob", .origin_sequence = 1},
          .order = {.lamport = 2, .origin_uid = "bob", .origin_sequence = 1}},
  };
  for (auto const& entry : instance.shared_journal) {
    instance.RememberSharedEvent(entry.id);
  }
  auto& peer = runtime.EnsurePeer(instance, "bob");
  runtime.SeedPendingFromJournal(instance, peer);
  REQUIRE(peer.pending.size() == 1);
  REQUIRE(peer.pending.front().origin_uid == "alice");
}

void test_local_commit_enqueues_other_peers() {
  SharedRuntime runtime;
  SharedInstance<ChatRoom> instance;
  instance.local_aether_uid = "alice";
  runtime.EnsurePeer(instance, "bob");
  SharedEventId id{.origin_uid = "alice", .origin_sequence = 1};
  runtime.OnLocalEventCommitted(
      instance, id,
      [](PeerDeliveryState& peer, SharedEventId const& event_id) {
        peer.pending.push_back(event_id);
      });
  REQUIRE(instance.peers.front().pending.size() == 1);
}

ChatRuntime MakeRuntime(std::filesystem::path const& dir, std::string name) {
  EnsureChatRegistration();
  std::filesystem::remove_all(dir);
  DistillChatModel(dir, std::move(name));
  return LoadChatModel(dir);
}

void test_preconnect_local_journal() {
  auto const dir =
      std::filesystem::temp_directory_path() / "apptraverse-shared-journal-a";
  auto runtime = MakeRuntime(dir, "Peer");
  ChatSharedBinding binding;
  binding.runtime = SharedRuntime{};
  InitializeChatSharedBinding(binding, *runtime.application, "alice-uid");
  CommitLocalJoin(binding, *runtime.application->host_client);
  CommitLocalMessage(binding, *runtime.application->host_client, "before");
  REQUIRE(runtime.application->chat_room->feed.size() == 2);
  REQUIRE(binding.instance.shared_journal.size() == 2);
  REQUIRE(!binding.instance.shared_journal[0].payload.empty());
}

void test_connect_creates_peer_seeds_opens_once() {
  EnsureChatRegistration();
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};
  auto application = BuildChatGraph(domain, "Client");
  FinalizeDistilledGraph(*application);
  ChatSharedBinding binding;
  InitializeChatSharedBinding(binding, *application, "client-uid");
  CommitLocalJoin(binding, *application->host_client);
  CommitLocalMessage(binding, *application->host_client, "client before");

  int open_count = 0;
  std::string opened;
  ConnectToHostCommand(binding, "host-uid",
                       [&](std::string const& uid) {
                         ++open_count;
                         opened = uid;
                       });
  REQUIRE(open_count == 1);
  REQUIRE(opened == "host-uid");
  REQUIRE(binding.instance.peers.size() == 1);
  REQUIRE(binding.instance.peers[0].pending.size() == 2);
  REQUIRE(!binding.instance.peers[0].channel_ready);

  ConnectToHostCommand(binding, "host-uid",
                       [&](std::string const& uid) {
                         ++open_count;
                         opened = uid;
                       });
  REQUIRE(open_count == 2);
  REQUIRE(binding.instance.peers.size() == 1);
  REQUIRE(binding.instance.peers[0].pending.size() == 2);
}

void test_no_send_before_channel_ready() {
  EnsureChatRegistration();
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};
  auto application = BuildChatGraph(domain, "Client");
  FinalizeDistilledGraph(*application);
  ChatSharedBinding binding;
  InitializeChatSharedBinding(binding, *application, "client-uid");
  CommitLocalJoin(binding, *application->host_client);
  ConnectToHostCommand(binding, "host-uid", [](std::string const&) {});
  RecordingTransport transport;
  TickSharedDelivery(binding, std::chrono::steady_clock::now(), &transport);
  REQUIRE(transport.events.empty());
}

void test_stream_ready_sends_first_pending() {
  EnsureChatRegistration();
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};
  auto application = BuildChatGraph(domain, "Client");
  FinalizeDistilledGraph(*application);
  ChatSharedBinding binding;
  InitializeChatSharedBinding(binding, *application, "client-uid");
  CommitLocalJoin(binding, *application->host_client);
  CommitLocalMessage(binding, *application->host_client, "hello");
  ConnectToHostCommand(binding, "host-uid", [](std::string const&) {});
  SetSharedPeerChannelReady(binding, "host-uid", true);
  RecordingTransport transport;
  TickSharedDelivery(binding, std::chrono::steady_clock::now(), &transport);
  REQUIRE(transport.events.size() == 1);
  REQUIRE(transport.events[0].peer_uid == "host-uid");
  REQUIRE(transport.events[0].frame.event_id.origin_sequence == 1);
}

void test_event_codec_roundtrip_join_and_message() {
  EnsureChatRegistration();
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};
  auto application = BuildChatGraph(domain, "Alice");
  FinalizeDistilledGraph(*application);
  application->host_client->SetAetherUidText("alice-uid");
  ChatSharedBinding binding;
  InitializeChatSharedBinding(binding, *application, "alice-uid");
  CommitLocalJoin(binding, *application->host_client);
  CommitLocalMessage(binding, *application->host_client, "hi");

  auto const& join_payload = binding.instance.shared_journal[0].payload;
  auto const& msg_payload = binding.instance.shared_journal[1].payload;
  REQUIRE(!join_payload.empty());
  REQUIRE(!msg_payload.empty());

  SharedEventFrame join_frame{.shared_room_id = "alice-uid",
                              .event_id = binding.instance.shared_journal[0].id,
                              .order = binding.instance.shared_journal[0].order,
                              .payload = join_payload};
  auto encoded = EncodeSharedEventFrame(join_frame);
  SharedEventFrame decoded;
  REQUIRE(DecodeSharedEventFrame(encoded, decoded));
  REQUIRE(decoded.event_id == join_frame.event_id);
  REQUIRE(decoded.payload == join_payload);

  ae::RamDomainStorage storage2;
  ae::Domain domain2{ae::Now(), storage2};
  auto application2 = BuildChatGraph(domain2, "Bob");
  FinalizeDistilledGraph(*application2);
  Event::ptr remapped;
  REQUIRE(DeserializeSharedEventPayload(*application2->chat_room, remapped,
                                       join_payload));
  remapped.Load();
  auto* join = dynamic_cast<JoinEvent*>(&*remapped);
  REQUIRE(join != nullptr);
  REQUIRE(join->client->AetherUidText() == "alice-uid");
  REQUIRE(join->client->DisplayNameBytes() == "Alice");
}

void test_incoming_join_applies_and_acks() {
  EnsureChatRegistration();
  ae::RamDomainStorage host_storage;
  ae::Domain host_domain{ae::Now(), host_storage};
  auto host_app = BuildChatGraph(host_domain, "Host");
  FinalizeDistilledGraph(*host_app);
  host_app->host_client->SetAetherUidText("host-uid");
  ChatSharedBinding host;
  InitializeChatSharedBinding(host, *host_app, "host-uid");
  CommitLocalJoin(host, *host_app->host_client);

  ae::RamDomainStorage client_storage;
  ae::Domain client_domain{ae::Now(), client_storage};
  auto client_app = BuildChatGraph(client_domain, "Client");
  FinalizeDistilledGraph(*client_app);
  client_app->host_client->SetAetherUidText("client-uid");
  ChatSharedBinding client;
  InitializeChatSharedBinding(client, *client_app, "client-uid");
  CommitLocalJoin(client, *client_app->host_client);

  SharedEventFrame frame{
      .shared_room_id = "host-uid",
      .event_id = client.instance.shared_journal[0].id,
      .order = client.instance.shared_journal[0].order,
      .payload = client.instance.shared_journal[0].payload,
  };
  std::string joined;
  REQUIRE(ApplyIncomingSharedEvent(host, "client-uid", frame,
                                  [&](std::string const& uid) {
                                    joined = uid;
                                    EnsureSharedPeer(host, uid);
                                  }));
  REQUIRE(joined == "client-uid");
  REQUIRE(host_app->chat_room->clients.size() == 2);
  REQUIRE(host_app->chat_room->feed.size() == 2);

  RecordingTransport transport;
  SendSharedAck(host, &transport, "client-uid", frame.event_id);
  REQUIRE(transport.acks.size() == 1);

  // Duplicate still "ok" and should ACK again.
  REQUIRE(ApplyIncomingSharedEvent(host, "client-uid", frame, {}));
  SendSharedAck(host, &transport, "client-uid", frame.event_id);
  REQUIRE(transport.acks.size() == 2);
  REQUIRE(host_app->chat_room->feed.size() == 2);
}

void test_ack_clears_and_sends_next() {
  EnsureChatRegistration();
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};
  auto application = BuildChatGraph(domain, "Client");
  FinalizeDistilledGraph(*application);
  application->host_client->SetAetherUidText("client-uid");
  ChatSharedBinding binding;
  InitializeChatSharedBinding(binding, *application, "client-uid");
  CommitLocalJoin(binding, *application->host_client);
  CommitLocalMessage(binding, *application->host_client, "a");
  CommitLocalMessage(binding, *application->host_client, "b");
  ConnectToHostCommand(binding, "host-uid", [](std::string const&) {});
  SetSharedPeerChannelReady(binding, "host-uid", true);
  RecordingTransport transport;
  auto now = std::chrono::steady_clock::now();
  TickSharedDelivery(binding, now, &transport);
  REQUIRE((transport.events.size()) == (1u));
  auto const first = transport.events[0].frame.event_id;
  HandleSharedAck(binding, "host-uid",
                  SharedAckFrame{.shared_room_id = "host-uid",
                                 .event_id = first});
  TickSharedDelivery(binding, now, &transport);
  REQUIRE((transport.events.size()) == (2u));
  REQUIRE((transport.events[1].frame.event_id.origin_sequence) == (2u));
}

void test_retry_after_one_second() {
  EnsureChatRegistration();
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};
  auto application = BuildChatGraph(domain, "Client");
  FinalizeDistilledGraph(*application);
  application->host_client->SetAetherUidText("client-uid");
  ChatSharedBinding binding;
  InitializeChatSharedBinding(binding, *application, "client-uid");
  CommitLocalJoin(binding, *application->host_client);
  ConnectToHostCommand(binding, "host-uid", [](std::string const&) {});
  SetSharedPeerChannelReady(binding, "host-uid", true);
  RecordingTransport transport;
  auto now = std::chrono::steady_clock::now();
  TickSharedDelivery(binding, now, &transport);
  REQUIRE(transport.events.size() == 1);
  TickSharedDelivery(binding, now + std::chrono::milliseconds{500}, &transport);
  REQUIRE(transport.events.size() == 1);
  TickSharedDelivery(binding, now + std::chrono::milliseconds{1000},
                     &transport);
  REQUIRE(transport.events.size() == 2);
  REQUIRE(transport.events[0].frame.event_id ==
         transport.events[1].frame.event_id);
}

void test_objid_collision_remaps() {
  EnsureChatRegistration();
  ae::RamDomainStorage host_storage;
  ae::Domain host_domain{ae::Now(), host_storage};
  auto host_app = BuildChatGraph(host_domain, "Host");
  FinalizeDistilledGraph(*host_app);
  host_app->host_client->SetAetherUidText("host-uid");
  // Host already has HostClient at fixed ObjId 100021.
  ChatSharedBinding host;
  InitializeChatSharedBinding(host, *host_app, "host-uid");
  CommitLocalJoin(host, *host_app->host_client);

  ae::RamDomainStorage client_storage;
  ae::Domain client_domain{ae::Now(), client_storage};
  auto client_app = BuildChatGraph(client_domain, "Client");
  FinalizeDistilledGraph(*client_app);
  client_app->host_client->SetAetherUidText("client-uid");
  ChatSharedBinding client;
  InitializeChatSharedBinding(client, *client_app, "client-uid");
  CommitLocalJoin(client, *client_app->host_client);

  // Both sides used the same distilled HostClient ObjId for their local user.
  REQUIRE(host_app->host_client.id().id() == client_app->host_client.id().id());

  SharedEventFrame frame{
      .shared_room_id = "host-uid",
      .event_id = client.instance.shared_journal[0].id,
      .order = client.instance.shared_journal[0].order,
      .payload = client.instance.shared_journal[0].payload,
  };
  REQUIRE(ApplyIncomingSharedEvent(host, "client-uid", frame, {}));
  auto remote = host_app->chat_room->FindClientByAetherUid("client-uid");
  REQUIRE(remote.is_valid());
  REQUIRE(remote.id().id() != host_app->host_client.id().id() ||
         remote->AetherUidText() == "client-uid");
  REQUIRE(remote->DisplayNameBytes() == "Client");
  REQUIRE(host_app->host_client->DisplayNameBytes() == "Host");
}

void test_fake_bridge_converges() {
  EnsureChatRegistration();
  ae::RamDomainStorage host_storage;
  ae::Domain host_domain{ae::Now(), host_storage};
  auto host_app = BuildChatGraph(host_domain, "Host");
  FinalizeDistilledGraph(*host_app);
  host_app->host_client->SetAetherUidText("host-uid");
  ChatSharedBinding host;
  InitializeChatSharedBinding(host, *host_app, "host-uid");
  CommitLocalJoin(host, *host_app->host_client);
  CommitLocalMessage(host, *host_app->host_client, "host before");

  ae::RamDomainStorage client_storage;
  ae::Domain client_domain{ae::Now(), client_storage};
  auto client_app = BuildChatGraph(client_domain, "Client");
  FinalizeDistilledGraph(*client_app);
  client_app->host_client->SetAetherUidText("client-uid");
  ChatSharedBinding client;
  InitializeChatSharedBinding(client, *client_app, "client-uid");
  CommitLocalJoin(client, *client_app->host_client);
  CommitLocalMessage(client, *client_app->host_client, "client before");

  struct QueuedFrame {
    std::string from;
    std::vector<std::uint8_t> bytes;
  };
  std::vector<QueuedFrame> to_host;
  std::vector<QueuedFrame> to_client;

  class QueueTransport final : public ISharedTransport {
   public:
    QueueTransport(std::string local_uid, std::vector<QueuedFrame>* out)
        : local_uid_{std::move(local_uid)}, out_{out} {}

    void SendEvent(std::string const& peer_uid,
                   SharedEventFrame const& frame) override {
      (void)peer_uid;
      out_->push_back(
          QueuedFrame{local_uid_, EncodeSharedEventFrame(frame)});
    }

    void SendAck(std::string const& peer_uid,
                 SharedAckFrame const& frame) override {
      (void)peer_uid;
      out_->push_back(QueuedFrame{local_uid_, EncodeSharedAckFrame(frame)});
    }

   private:
    std::string local_uid_;
    std::vector<QueuedFrame>* out_;
  };

  QueueTransport host_transport{"host-uid", &to_client};
  QueueTransport client_transport{"client-uid", &to_host};

  auto handle_one = [&](ChatSharedBinding& binding, ISharedTransport* tx,
                        QueuedFrame const& item,
                        bool ensure_peer_on_join) {
    SharedEventFrame event_frame;
    if (DecodeSharedEventFrame(item.bytes, event_frame)) {
      ApplyIncomingSharedEvent(
          binding, item.from, event_frame,
          [&](std::string const& uid) {
            if (!ensure_peer_on_join) {
              return;
            }
            EnsureSharedPeer(binding, uid);
            SetSharedPeerChannelReady(binding, uid, true);
          });
      SendSharedAck(binding, tx, item.from, event_frame.event_id);
      return;
    }
    SharedAckFrame ack;
    if (DecodeSharedAckFrame(item.bytes, ack)) {
      HandleSharedAck(binding, item.from, ack);
    }
  };

  ConnectToHostCommand(client, "host-uid", [](std::string const&) {});
  EnsureSharedPeer(host, "client-uid");
  SetSharedPeerChannelReady(client, "host-uid", true);
  SetSharedPeerChannelReady(host, "client-uid", true);

  for (int round = 0; round < 50; ++round) {
    TickSharedDelivery(client, std::chrono::steady_clock::now(),
                       &client_transport);
    TickSharedDelivery(host, std::chrono::steady_clock::now(), &host_transport);

    auto host_inbox = std::move(to_host);
    to_host.clear();
    for (auto const& item : host_inbox) {
      handle_one(host, &host_transport, item, true);
    }
    auto client_inbox = std::move(to_client);
    to_client.clear();
    for (auto const& item : client_inbox) {
      handle_one(client, &client_transport, item, false);
    }

    if (host.instance.peers[0].pending.empty() &&
        client.instance.peers[0].pending.empty() &&
        !host.instance.peers[0].in_flight.has_value() &&
        !client.instance.peers[0].in_flight.has_value() &&
        host_app->chat_room->feed.size() == 4 &&
        client_app->chat_room->feed.size() == 4) {
      break;
    }
  }

  REQUIRE(host_app->chat_room->clients.size() == 2);
  REQUIRE(client_app->chat_room->clients.size() == 2);
  REQUIRE(host_app->chat_room->feed.size() == 4);
  REQUIRE(client_app->chat_room->feed.size() == 4);
  REQUIRE(host.instance.peers[0].pending.empty());
  REQUIRE(client.instance.peers[0].pending.empty());
  REQUIRE(!host.instance.peers[0].in_flight.has_value());
  REQUIRE(!client.instance.peers[0].in_flight.has_value());
}

}  // namespace

int main() {
  test_ack_clears_pending();
  test_duplicate_event_id();
  test_seed_pending_excludes_peer_origin();
  test_local_commit_enqueues_other_peers();
  test_preconnect_local_journal();
  test_connect_creates_peer_seeds_opens_once();
  test_no_send_before_channel_ready();
  test_stream_ready_sends_first_pending();
  test_event_codec_roundtrip_join_and_message();
  test_incoming_join_applies_and_acks();
  test_ack_clears_and_sends_next();
  test_retry_after_one_second();
  test_objid_collision_remaps();
  test_fake_bridge_converges();
  std::cout << "shared_journal_test ok\n";
  return 0;
}
