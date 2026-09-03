#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "aether/clock.h"
#include "aether-objects/domain_storage/ram_domain_storage.h"

#include "apptraverse/shared_event_id.h"
#include "apptraverse/shared_event_order.h"
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

  SharedTransportEnqueueResult SendEvent(std::string const& peer_uid,
                                         SharedEventFrame const& frame) override {
    events.push_back(SentEvent{peer_uid, frame});
    return SharedTransportEnqueueResult::Queued;
  }

  SharedTransportEnqueueResult SendAck(std::string const& peer_uid,
                                       SharedAckFrame const& frame) override {
    acks.push_back(SentAck{peer_uid, frame});
    return SharedTransportEnqueueResult::Queued;
  }
};

SharedEventFrame FrameFromJournalRecord(ChatSharedBinding const& binding,
                                        std::size_t index) {
  REQUIRE(binding.instance.node.is_valid());
  REQUIRE(index < binding.instance.node->journal.size());
  auto const& record = binding.instance.node->journal[index];
  REQUIRE(record.HasSharedIdentity());
  REQUIRE(record.event.is_valid());
  record.event.Load();
  return SharedEventFrame{
      .shared_room_id = binding.instance.shared_room_id,
      .event_id = record.identity,
      .order = record.order,
      .payload = SerializeSharedEventPayload(*record.event),
  };
}

std::string JournalEventTypeName(EventRecord const& record) {
  REQUIRE(record.event.is_valid());
  record.event.Load();
  if (dynamic_cast<JoinEvent const*>(&*record.event) != nullptr) {
    return "join";
  }
  if (dynamic_cast<ChatMessageEvent const*>(&*record.event) != nullptr) {
    return "message";
  }
  return "unknown";
}

std::string JournalAuthorUid(EventRecord const& record) {
  REQUIRE(record.event.is_valid());
  record.event.Load();
  if (auto const* join = dynamic_cast<JoinEvent const*>(&*record.event)) {
    REQUIRE(join->client.is_valid());
    return join->client->AetherUidText();
  }
  if (auto const* message =
          dynamic_cast<ChatMessageEvent const*>(&*record.event)) {
    REQUIRE(message->author.is_valid());
    return message->author->AetherUidText();
  }
  return {};
}

std::string JournalMessageText(EventRecord const& record) {
  REQUIRE(record.event.is_valid());
  record.event.Load();
  auto const* message = dynamic_cast<ChatMessageEvent const*>(&*record.event);
  if (message == nullptr || !message->text.is_valid()) {
    return {};
  }
  message->text.Load();
  return message->text->bytes;
}

std::int64_t JournalSentAt(EventRecord const& record) {
  REQUIRE(record.event.is_valid());
  record.event.Load();
  auto const* message = dynamic_cast<ChatMessageEvent const*>(&*record.event);
  if (message == nullptr) {
    return 0;
  }
  return message->sent_at_unix_ms;
}

std::vector<std::string> SortedClientUids(ChatRoom const& room) {
  std::vector<std::string> uids;
  for (auto const& client : room.clients) {
    if (!client.is_valid()) {
      continue;
    }
    client.Load();
    uids.push_back(client->AetherUidText());
  }
  std::sort(uids.begin(), uids.end());
  return uids;
}

void RequireMembershipConverged(ChatRoom const& left, ChatRoom const& right) {
  REQUIRE(SortedClientUids(left) == SortedClientUids(right));
}

void RequireJournalsConverged(ChatRoom const& left, ChatRoom const& right) {
  REQUIRE(left.journal.size() == right.journal.size());
  for (std::size_t i = 0; i < left.journal.size(); ++i) {
    auto const& a = left.journal[i];
    auto const& b = right.journal[i];
    REQUIRE(a.HasSharedIdentity());
    REQUIRE(b.HasSharedIdentity());
    REQUIRE(a.identity == b.identity);
    REQUIRE(a.order == b.order);
    REQUIRE(JournalEventTypeName(a) == JournalEventTypeName(b));
    REQUIRE(JournalAuthorUid(a) == JournalAuthorUid(b));
    REQUIRE(JournalMessageText(a) == JournalMessageText(b));
    REQUIRE(JournalSentAt(a) == JournalSentAt(b));
  }
}

void RequireJournalSortedBySharedOrder(ChatRoom const& room) {
  for (std::size_t i = 1; i < room.journal.size(); ++i) {
    REQUIRE(SharedEventOrderLess(room.journal[i - 1].order,
                                 room.journal[i].order));
  }
}

Application::ptr MakeChatApp(ae::Domain& domain, std::string name,
                             std::string uid) {
  EnsureChatRegistration();
  auto application = BuildChatGraph(domain, std::move(name));
  FinalizeDistilledGraph(*application);
  application->host_client->SetAetherUidText(uid);
  return application;
}

ChatSharedBinding BindChat(Application& application, std::string uid) {
  ChatSharedBinding binding;
  InitializeChatSharedBinding(binding, application, std::move(uid));
  return binding;
}

void test_pipeline_sends_multiple_without_ack() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto application = MakeChatApp(domain, "Client", "client-uid");
  auto binding = BindChat(*application, "client-uid");
  CommitLocalJoin(binding, *application->host_client);
  ConnectToHostCommand(binding, "host-uid", [](std::string const&) {});
  auto* peer = binding.instance.FindPeer("host-uid");
  peer->channel_ready = true;
  peer->pending.push_back(
      SharedEventId{.origin_uid = "client-uid", .origin_sequence = 1});
  peer->pending.push_back(
      SharedEventId{.origin_uid = "client-uid", .origin_sequence = 2});
  peer->pending.push_back(
      SharedEventId{.origin_uid = "client-uid", .origin_sequence = 3});
  int sends = 0;
  binding.runtime.Tick(
      binding.instance, std::chrono::steady_clock::now(),
      [&](PeerDeliveryState&, SharedEventId const&) {
        ++sends;
        return true;
      });
  REQUIRE(sends == 3);
  REQUIRE(peer->in_flight.size() == 3);
}

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
  peer.in_flight.push_back(PeerInFlightEntry{.id = e1});
  runtime.OnAckReceived(instance, "bob", e1);
  REQUIRE(peer.in_flight.empty());
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
  REQUIRE(instance.HasSharedEvent(id));
}

void test_seed_pending_excludes_peer_origin() {
  SharedRuntime runtime;
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto room = ChatRoom::ptr::Create(ae::CreateWith{domain});
  SharedInstance<ChatRoom> instance;
  instance.local_aether_uid = "alice";
  instance.node = room;
  instance.node->journal = {
      EventRecord{
          .event = {},
          .identity = {.origin_uid = "alice", .origin_sequence = 1},
          .order = {.lamport = 1, .origin_uid = "alice", .origin_sequence = 1}},
      EventRecord{
          .event = {},
          .identity = {.origin_uid = "bob", .origin_sequence = 1},
          .order = {.lamport = 2, .origin_uid = "bob", .origin_sequence = 1}},
  };
  for (auto const& record : instance.node->journal) {
    instance.RememberSharedEvent(record.identity);
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
  REQUIRE(binding.instance.node->journal.size() == 2);
  REQUIRE(binding.instance.node->journal[0].HasSharedIdentity());
  REQUIRE(binding.instance.node->journal[1].HasSharedIdentity());
  auto const* join_record =
      binding.instance.FindJournalRecord(binding.instance.node->journal[0].identity);
  REQUIRE(join_record != nullptr);
  join_record->event.Load();
  REQUIRE(!SerializeSharedEventPayload(*join_record->event).empty());
}

void test_connect_creates_peer_seeds_opens_once() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto application = MakeChatApp(domain, "Client", "client-uid");
  auto binding = BindChat(*application, "client-uid");
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
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto application = MakeChatApp(domain, "Client", "client-uid");
  auto binding = BindChat(*application, "client-uid");
  CommitLocalJoin(binding, *application->host_client);
  ConnectToHostCommand(binding, "host-uid", [](std::string const&) {});
  RecordingTransport transport;
  TickSharedDelivery(binding, std::chrono::steady_clock::now(), &transport);
  REQUIRE(transport.events.empty());
}

void test_stream_ready_sends_all_pending() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto application = MakeChatApp(domain, "Client", "client-uid");
  auto binding = BindChat(*application, "client-uid");
  CommitLocalJoin(binding, *application->host_client);
  CommitLocalMessage(binding, *application->host_client, "hello");
  ConnectToHostCommand(binding, "host-uid", [](std::string const&) {});
  SetSharedPeerChannelReady(binding, "host-uid", true);
  RecordingTransport transport;
  TickSharedDelivery(binding, std::chrono::steady_clock::now(), &transport);
  REQUIRE(transport.events.size() == 2);
  REQUIRE(transport.events[0].peer_uid == "host-uid");
  REQUIRE(transport.events[0].frame.event_id.origin_sequence == 1);
  REQUIRE(transport.events[0].frame.order ==
          binding.instance.node->journal[0].order);
  REQUIRE(transport.events[1].frame.event_id.origin_sequence == 2);
}

void test_event_codec_roundtrip_join_and_message() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto application = MakeChatApp(domain, "Alice", "alice-uid");
  auto binding = BindChat(*application, "alice-uid");
  CommitLocalJoin(binding, *application->host_client);
  CommitLocalMessage(binding, *application->host_client, "hi");

  auto const& join_record = binding.instance.node->journal[0];
  auto const& msg_record = binding.instance.node->journal[1];
  join_record.event.Load();
  msg_record.event.Load();
  auto const join_payload = SerializeSharedEventPayload(*join_record.event);
  auto const msg_payload = SerializeSharedEventPayload(*msg_record.event);
  REQUIRE(!join_payload.empty());
  REQUIRE(!msg_payload.empty());

  SharedEventFrame join_frame{.shared_room_id = "alice-uid",
                              .event_id = join_record.identity,
                              .order = join_record.order,
                              .payload = join_payload};
  auto encoded = EncodeSharedEventFrame(join_frame);
  SharedEventFrame decoded;
  REQUIRE(DecodeSharedEventFrame(encoded, decoded));
  REQUIRE(decoded.event_id == join_frame.event_id);
  REQUIRE(decoded.order == join_frame.order);
  REQUIRE(decoded.payload == join_payload);

  ae::RamDomainStorage storage2;
  ae::Domain domain2{storage2};
  auto application2 = MakeChatApp(domain2, "Bob", "bob-uid");
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
  ae::RamDomainStorage host_storage;
  ae::Domain host_domain{host_storage};
  auto host_app = MakeChatApp(host_domain, "Host", "host-uid");
  auto host = BindChat(*host_app, "host-uid");
  CommitLocalJoin(host, *host_app->host_client);

  ae::RamDomainStorage client_storage;
  ae::Domain client_domain{client_storage};
  auto client_app = MakeChatApp(client_domain, "Client", "client-uid");
  auto client = BindChat(*client_app, "client-uid");
  CommitLocalJoin(client, *client_app->host_client);

  SharedEventFrame frame = FrameFromJournalRecord(client, 0);
  std::string joined;
  auto const applied = ApplyIncomingSharedEvent(
      host, "client-uid", frame, [&](std::string const& uid) {
        joined = uid;
        EnsureSharedPeer(host, uid);
      });
  REQUIRE(applied == SharedApplyResult::Applied);
  REQUIRE(SharedApplyResultAllowsAck(applied));
  REQUIRE(joined == "client-uid");
  REQUIRE(host_app->chat_room->clients.size() == 2);
  REQUIRE(host_app->chat_room->feed.size() == 2);

  RecordingTransport transport;
  SendSharedAck(host, &transport, "client-uid", frame.event_id);
  REQUIRE(transport.acks.size() == 1);

  auto const duplicate =
      ApplyIncomingSharedEvent(host, "client-uid", frame, {});
  REQUIRE(duplicate == SharedApplyResult::DuplicateAlreadyApplied);
  REQUIRE(SharedApplyResultAllowsAck(duplicate));
  SendSharedAck(host, &transport, "client-uid", frame.event_id);
  REQUIRE(transport.acks.size() == 2);
  REQUIRE(host_app->chat_room->feed.size() == 2);
}

void test_message_before_join_is_deferred_then_drained() {
  ae::RamDomainStorage host_storage;
  ae::Domain host_domain{host_storage};
  auto host_app = MakeChatApp(host_domain, "Host", "host-uid");
  auto host = BindChat(*host_app, "host-uid");
  CommitLocalJoin(host, *host_app->host_client);

  ae::RamDomainStorage client_storage;
  ae::Domain client_domain{client_storage};
  auto client_app = MakeChatApp(client_domain, "Client", "client-uid");
  auto client = BindChat(*client_app, "client-uid");
  CommitLocalJoin(client, *client_app->host_client);
  CommitLocalMessage(client, *client_app->host_client, "early");

  SharedEventFrame message_frame = FrameFromJournalRecord(client, 1);
  SharedEventFrame join_frame = FrameFromJournalRecord(client, 0);

  auto const deferred =
      ApplyIncomingSharedEvent(host, "client-uid", message_frame, {});
  REQUIRE(deferred == SharedApplyResult::Deferred);
  REQUIRE(!SharedApplyResultAllowsAck(deferred));
  REQUIRE(!host.instance.HasSharedEvent(message_frame.event_id));
  REQUIRE(host.instance.deferred.size() == 1);
  REQUIRE(host_app->chat_room->feed.size() == 1);

  RecordingTransport transport;
  // Deferred must not take the ACK path.
  REQUIRE(!SharedApplyResultAllowsAck(deferred));

  auto const join_applied =
      ApplyIncomingSharedEvent(host, "client-uid", join_frame,
                               [&](std::string const& uid) {
                                 EnsureSharedPeer(host, uid);
                               });
  REQUIRE(join_applied == SharedApplyResult::Applied);
  REQUIRE(SharedApplyResultAllowsAck(join_applied));
  REQUIRE(host.instance.HasSharedEvent(join_frame.event_id));
  REQUIRE(host.instance.HasSharedEvent(message_frame.event_id));
  REQUIRE(host.instance.deferred.empty());
  REQUIRE(host_app->chat_room->clients.size() == 2);
  REQUIRE(host_app->chat_room->feed.size() == 3);
  REQUIRE(JournalMessageText(host_app->chat_room->journal.back()) == "early");
}

void test_ack_clears_in_flight_without_blocking_pipeline() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto application = MakeChatApp(domain, "Client", "client-uid");
  auto binding = BindChat(*application, "client-uid");
  CommitLocalJoin(binding, *application->host_client);
  CommitLocalMessage(binding, *application->host_client, "a");
  CommitLocalMessage(binding, *application->host_client, "b");
  ConnectToHostCommand(binding, "host-uid", [](std::string const&) {});
  SetSharedPeerChannelReady(binding, "host-uid", true);
  RecordingTransport transport;
  auto now = std::chrono::steady_clock::now();
  TickSharedDelivery(binding, now, &transport);
  REQUIRE(transport.events.size() == 3);
  auto* peer = binding.instance.FindPeer("host-uid");
  REQUIRE(peer->in_flight.size() == 3);
  auto const first = transport.events[0].frame.event_id;
  HandleSharedAck(binding, "host-uid",
                  SharedAckFrame{.shared_room_id = "host-uid",
                                 .event_id = first});
  REQUIRE(peer->in_flight.size() == 2);
  TickSharedDelivery(binding, now, &transport);
  REQUIRE(transport.events.size() == 3);
}

void test_retry_after_one_second() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto application = MakeChatApp(domain, "Client", "client-uid");
  auto binding = BindChat(*application, "client-uid");
  CommitLocalJoin(binding, *application->host_client);
  ConnectToHostCommand(binding, "host-uid", [](std::string const&) {});
  SetSharedPeerChannelReady(binding, "host-uid", true);
  SetSharedPeerOnline(binding, "host-uid", true);
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

void test_retry_skipped_while_offline() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto application = MakeChatApp(domain, "Client", "client-uid");
  auto binding = BindChat(*application, "client-uid");
  CommitLocalJoin(binding, *application->host_client);
  ConnectToHostCommand(binding, "host-uid", [](std::string const&) {});
  // Channel down + offline: retry must wait for presence/reopen evidence.
  SetSharedPeerChannelReady(binding, "host-uid", false);
  auto* peer = binding.instance.FindPeer("host-uid");
  peer->channel_ready = false;
  peer->pending.push_back(
      SharedEventId{.origin_uid = "client-uid", .origin_sequence = 1});
  peer->in_flight.push_back(PeerInFlightEntry{
      .id = peer->pending.front(),
      .first_sent_at = std::chrono::steady_clock::now(),
      .last_sent_at = std::chrono::steady_clock::now(),
  });
  RecordingTransport transport;
  auto now = peer->in_flight.front().last_sent_at;
  TickSharedDelivery(binding, now + std::chrono::milliseconds{1000},
                     &transport);
  REQUIRE(transport.events.empty());

  // Ready stream is transport evidence: retry even while presence is false.
  peer->channel_ready = true;
  TickSharedDelivery(binding, now + std::chrono::milliseconds{1000},
                     &transport);
  REQUIRE(transport.events.size() == 1);
}

void test_objid_collision_remaps() {
  ae::RamDomainStorage host_storage;
  ae::Domain host_domain{host_storage};
  auto host_app = MakeChatApp(host_domain, "Host", "host-uid");
  // Host already has HostClient at fixed ObjId 100021.
  auto host = BindChat(*host_app, "host-uid");
  CommitLocalJoin(host, *host_app->host_client);

  ae::RamDomainStorage client_storage;
  ae::Domain client_domain{client_storage};
  auto client_app = MakeChatApp(client_domain, "Client", "client-uid");
  auto client = BindChat(*client_app, "client-uid");
  CommitLocalJoin(client, *client_app->host_client);

  // Both sides used the same distilled HostClient ObjId for their local user.
  REQUIRE(host_app->host_client.id().id() == client_app->host_client.id().id());

  SharedEventFrame frame = FrameFromJournalRecord(client, 0);
  REQUIRE(ApplyIncomingSharedEvent(host, "client-uid", frame, {}) ==
          SharedApplyResult::Applied);
  auto remote = host_app->chat_room->FindClientByAetherUid("client-uid");
  REQUIRE(remote.is_valid());
  REQUIRE(remote.id().id() != host_app->host_client.id().id() ||
         remote->AetherUidText() == "client-uid");
  REQUIRE(remote->DisplayNameBytes() == "Client");
  REQUIRE(host_app->host_client->DisplayNameBytes() == "Host");
}

struct QueuedFrame {
  std::string from;
  std::vector<std::uint8_t> bytes;
};

class QueueTransport final : public ISharedTransport {
 public:
  QueueTransport(std::string local_uid, std::vector<QueuedFrame>* out)
      : local_uid_{std::move(local_uid)}, out_{out} {}

  SharedTransportEnqueueResult SendEvent(std::string const& peer_uid,
                                         SharedEventFrame const& frame) override {
    (void)peer_uid;
    out_->push_back(QueuedFrame{local_uid_, EncodeSharedEventFrame(frame)});
    return SharedTransportEnqueueResult::Queued;
  }

  SharedTransportEnqueueResult SendAck(std::string const& peer_uid,
                                       SharedAckFrame const& frame) override {
    (void)peer_uid;
    out_->push_back(QueuedFrame{local_uid_, EncodeSharedAckFrame(frame)});
    return SharedTransportEnqueueResult::Queued;
  }

 private:
  std::string local_uid_;
  std::vector<QueuedFrame>* out_;
};

void HandleQueuedFrame(ChatSharedBinding& binding, ISharedTransport* tx,
                       QueuedFrame const& item, bool ensure_peer_on_join) {
  SharedEventFrame event_frame;
  if (DecodeSharedEventFrame(item.bytes, event_frame)) {
    auto const result = ApplyIncomingSharedEvent(
        binding, item.from, event_frame,
        [&](std::string const& uid) {
          if (!ensure_peer_on_join) {
            return;
          }
          EnsureSharedPeer(binding, uid);
          SetSharedPeerChannelReady(binding, uid, true);
        });
    if (SharedApplyResultAllowsAck(result)) {
      SendSharedAck(binding, tx, item.from, event_frame.event_id);
    }
    return;
  }
  SharedAckFrame ack;
  if (DecodeSharedAckFrame(item.bytes, ack)) {
    HandleSharedAck(binding, item.from, ack);
  }
}

void RunFakeBridgeUntilIdle(ChatSharedBinding& host, ChatSharedBinding& client,
                            Application& host_app, Application& client_app,
                            std::vector<QueuedFrame>& to_host,
                            std::vector<QueuedFrame>& to_client,
                            QueueTransport& host_transport,
                            QueueTransport& client_transport,
                            std::size_t expected_feed_size) {
  for (int round = 0; round < 80; ++round) {
    TickSharedDelivery(client, std::chrono::steady_clock::now(),
                       &client_transport);
    TickSharedDelivery(host, std::chrono::steady_clock::now(), &host_transport);

    auto host_inbox = std::move(to_host);
    to_host.clear();
    for (auto const& item : host_inbox) {
      HandleQueuedFrame(host, &host_transport, item, true);
    }
    auto client_inbox = std::move(to_client);
    to_client.clear();
    for (auto const& item : client_inbox) {
      HandleQueuedFrame(client, &client_transport, item, false);
    }

    if (!host.instance.peers[0].HasOutstanding() &&
        !client.instance.peers[0].HasOutstanding() &&
        host_app.chat_room->feed.size() == expected_feed_size &&
        client_app.chat_room->feed.size() == expected_feed_size &&
        host.instance.deferred.empty() && client.instance.deferred.empty()) {
      return;
    }
  }
  REQUIRE(false && "fake bridge did not converge");
}

void test_fake_bridge_converges() {
  ae::RamDomainStorage host_storage;
  ae::Domain host_domain{host_storage};
  auto host_app = MakeChatApp(host_domain, "Host", "host-uid");
  auto host = BindChat(*host_app, "host-uid");
  CommitLocalJoin(host, *host_app->host_client);
  CommitLocalMessage(host, *host_app->host_client, "host before");

  ae::RamDomainStorage client_storage;
  ae::Domain client_domain{client_storage};
  auto client_app = MakeChatApp(client_domain, "Client", "client-uid");
  auto client = BindChat(*client_app, "client-uid");
  CommitLocalJoin(client, *client_app->host_client);
  CommitLocalMessage(client, *client_app->host_client, "client before");

  std::vector<QueuedFrame> to_host;
  std::vector<QueuedFrame> to_client;
  QueueTransport host_transport{"host-uid", &to_client};
  QueueTransport client_transport{"client-uid", &to_host};

  ConnectToHostCommand(client, "host-uid", [](std::string const&) {});
  EnsureSharedPeer(host, "client-uid");
  SetSharedPeerChannelReady(client, "host-uid", true);
  SetSharedPeerChannelReady(host, "client-uid", true);

  RunFakeBridgeUntilIdle(host, client, *host_app, *client_app, to_host,
                         to_client, host_transport, client_transport, 4);

  REQUIRE(host_app->chat_room->clients.size() == 2);
  REQUIRE(client_app->chat_room->clients.size() == 2);
  REQUIRE(host_app->chat_room->feed.size() == 4);
  REQUIRE(client_app->chat_room->feed.size() == 4);
  REQUIRE(host.instance.peers[0].pending.empty());
  REQUIRE(client.instance.peers[0].pending.empty());
  REQUIRE(!host.instance.peers[0].HasOutstanding());
  REQUIRE(!client.instance.peers[0].HasOutstanding());
  RequireJournalsConverged(*host_app->chat_room, *client_app->chat_room);
  RequireMembershipConverged(*host_app->chat_room, *client_app->chat_room);
}

void test_exact_journal_convergence_host_vs_client() {
  ae::RamDomainStorage host_storage;
  ae::Domain host_domain{host_storage};
  auto host_app = MakeChatApp(host_domain, "Host", "host-uid");
  auto host = BindChat(*host_app, "host-uid");
  CommitLocalJoin(host, *host_app->host_client);
  CommitLocalMessage(host, *host_app->host_client, "from-host", 1'700'000'000'001LL);

  ae::RamDomainStorage client_storage;
  ae::Domain client_domain{client_storage};
  auto client_app = MakeChatApp(client_domain, "Client", "client-uid");
  auto client = BindChat(*client_app, "client-uid");
  CommitLocalJoin(client, *client_app->host_client);
  CommitLocalMessage(client, *client_app->host_client, "from-client",
                     1'700'000'000'002LL);

  std::vector<QueuedFrame> to_host;
  std::vector<QueuedFrame> to_client;
  QueueTransport host_transport{"host-uid", &to_client};
  QueueTransport client_transport{"client-uid", &to_host};

  ConnectToHostCommand(client, "host-uid", [](std::string const&) {});
  EnsureSharedPeer(host, "client-uid");
  SetSharedPeerChannelReady(client, "host-uid", true);
  SetSharedPeerChannelReady(host, "client-uid", true);

  RunFakeBridgeUntilIdle(host, client, *host_app, *client_app, to_host,
                         to_client, host_transport, client_transport, 4);

  RequireJournalsConverged(*host_app->chat_room, *client_app->chat_room);
  RequireMembershipConverged(*host_app->chat_room, *client_app->chat_room);
  RequireJournalSortedBySharedOrder(*host_app->chat_room);
  RequireJournalSortedBySharedOrder(*client_app->chat_room);
  REQUIRE(host_app->chat_room->journal.size() == 4);
  std::size_t join_count = 0;
  std::size_t message_count = 0;
  for (auto const& record : host_app->chat_room->journal) {
    auto const type = JournalEventTypeName(record);
    if (type == "join") {
      ++join_count;
    } else if (type == "message") {
      ++message_count;
    }
  }
  REQUIRE(join_count == 2);
  REQUIRE(message_count == 2);
}

void test_interleaved_sent_at_converges_by_shared_order() {
  // Screenshot-style regression: local commits with intentionally scrambled
  // sent_at must still converge to SharedEventOrder on both replicas.
  ae::RamDomainStorage host_storage;
  ae::Domain host_domain{host_storage};
  auto host_app = MakeChatApp(host_domain, "Host", "host-uid");
  auto host = BindChat(*host_app, "host-uid");
  CommitLocalJoin(host, *host_app->host_client);
  // Higher wall-clock first, then earlier wall-clock — SharedEventOrder uses
  // lamport, not sent_at.
  CommitLocalMessage(host, *host_app->host_client, "host-late-wall",
                     9'000'000'000'000LL);
  CommitLocalMessage(host, *host_app->host_client, "host-early-wall",
                     1'000'000'000'000LL);

  ae::RamDomainStorage client_storage;
  ae::Domain client_domain{client_storage};
  auto client_app = MakeChatApp(client_domain, "Client", "client-uid");
  auto client = BindChat(*client_app, "client-uid");
  CommitLocalJoin(client, *client_app->host_client);
  CommitLocalMessage(client, *client_app->host_client, "client-mid-wall",
                     5'000'000'000'000LL);

  std::vector<QueuedFrame> to_host;
  std::vector<QueuedFrame> to_client;
  QueueTransport host_transport{"host-uid", &to_client};
  QueueTransport client_transport{"client-uid", &to_host};

  ConnectToHostCommand(client, "host-uid", [](std::string const&) {});
  EnsureSharedPeer(host, "client-uid");
  SetSharedPeerChannelReady(client, "host-uid", true);
  SetSharedPeerChannelReady(host, "client-uid", true);

  RunFakeBridgeUntilIdle(host, client, *host_app, *client_app, to_host,
                         to_client, host_transport, client_transport, 5);

  RequireJournalsConverged(*host_app->chat_room, *client_app->chat_room);
  RequireMembershipConverged(*host_app->chat_room, *client_app->chat_room);
  RequireJournalSortedBySharedOrder(*host_app->chat_room);
  RequireJournalSortedBySharedOrder(*client_app->chat_room);

  // Local host commits stay in SharedEventOrder (lamport), not sent_at order.
  auto const& host_journal = host_app->chat_room->journal;
  std::vector<std::string> host_origin_texts;
  for (auto const& record : host_journal) {
    if (JournalAuthorUid(record) == "host-uid" &&
        JournalEventTypeName(record) == "message") {
      host_origin_texts.push_back(JournalMessageText(record));
    }
  }
  REQUIRE(host_origin_texts.size() == 2);
  REQUIRE(host_origin_texts[0] == "host-late-wall");
  REQUIRE(host_origin_texts[1] == "host-early-wall");
  // Explicit: host messages keep SharedEventOrder commit order despite sent_at.
  std::size_t late_index = host_journal.size();
  std::size_t early_index = host_journal.size();
  for (std::size_t i = 0; i < host_journal.size(); ++i) {
    if (JournalMessageText(host_journal[i]) == "host-late-wall") {
      late_index = i;
    }
    if (JournalMessageText(host_journal[i]) == "host-early-wall") {
      early_index = i;
    }
  }
  REQUIRE(late_index < early_index);
  REQUIRE(JournalSentAt(host_journal[late_index]) >
          JournalSentAt(host_journal[early_index]));
}

// Simultaneous local commits: same-turn Host/Client messages must converge to
// one SharedEventOrder (lamport, origin_uid, origin_sequence) on both sides.
void test_simultaneous_local_messages_converge() {
  ae::RamDomainStorage host_storage;
  ae::Domain host_domain{host_storage};
  auto host_app = MakeChatApp(host_domain, "Host", "host-uid");
  auto host = BindChat(*host_app, "host-uid");
  CommitLocalJoin(host, *host_app->host_client);

  ae::RamDomainStorage client_storage;
  ae::Domain client_domain{client_storage};
  auto client_app = MakeChatApp(client_domain, "Client", "client-uid");
  auto client = BindChat(*client_app, "client-uid");
  CommitLocalJoin(client, *client_app->host_client);

  ConnectToHostCommand(client, "host-uid", [](std::string const&) {});
  EnsureSharedPeer(host, "client-uid");
  SetSharedPeerChannelReady(host, "client-uid", true);
  SetSharedPeerChannelReady(client, "host-uid", true);

  // Force identical lamport clocks so origin_uid breaks the tie.
  host.instance.lamport_clock = 100;
  client.instance.lamport_clock = 100;
  CommitLocalMessage(host, *host_app->host_client, "H-simul", 50);
  CommitLocalMessage(client, *client_app->host_client, "C-simul", 50);
  REQUIRE(host.instance.node->journal.back().order.lamport ==
          client.instance.node->journal.back().order.lamport);

  std::vector<QueuedFrame> to_host;
  std::vector<QueuedFrame> to_client;
  QueueTransport host_transport{"host-uid", &to_client};
  QueueTransport client_transport{"client-uid", &to_host};
  RunFakeBridgeUntilIdle(host, client, *host_app, *client_app, to_host,
                         to_client, host_transport, client_transport, 4);
  RequireJournalsConverged(*host_app->chat_room, *client_app->chat_room);
  RequireMembershipConverged(*host_app->chat_room, *client_app->chat_room);
  auto const& hj = host_app->chat_room->journal;
  auto const& cj = client_app->chat_room->journal;
  REQUIRE(hj.size() == 4);
  REQUIRE(cj.size() == 4);
  // With equal lamport, lexicographic origin_uid: "client-uid" < "host-uid".
  REQUIRE(JournalMessageText(hj[2]) == "C-simul");
  REQUIRE(JournalMessageText(hj[3]) == "H-simul");
  REQUIRE(JournalMessageText(cj[2]) == "C-simul");
  REQUIRE(JournalMessageText(cj[3]) == "H-simul");
}

}  // namespace

int main() {
  test_pipeline_sends_multiple_without_ack();
  test_ack_clears_pending();
  test_duplicate_event_id();
  test_seed_pending_excludes_peer_origin();
  test_local_commit_enqueues_other_peers();
  test_preconnect_local_journal();
  test_connect_creates_peer_seeds_opens_once();
  test_no_send_before_channel_ready();
  test_stream_ready_sends_all_pending();
  test_event_codec_roundtrip_join_and_message();
  test_incoming_join_applies_and_acks();
  test_message_before_join_is_deferred_then_drained();
  test_ack_clears_in_flight_without_blocking_pipeline();
  test_retry_after_one_second();
  test_retry_skipped_while_offline();
  test_objid_collision_remaps();
  test_fake_bridge_converges();
  test_exact_journal_convergence_host_vs_client();
  test_interleaved_sent_at_converges_by_shared_order();
  test_simultaneous_local_messages_converge();
  std::cout << "shared_journal_test ok\n";
  return 0;
}
