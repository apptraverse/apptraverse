#include "chat_shared.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <stdexcept>
#include <utility>

#include "aether/domain_storage/ram_domain_storage.h"

#include "apptraverse/object_serialization.h"
#include "apptraverse/shared_event_order.h"
#include "apptraverse/shared_frame_codec.h"

#include "chat_commands.h"
#include "chat_events.h"
#include "chat_log.h"

namespace apptraverse {
namespace {

void EnqueuePending(PeerDeliveryState& peer, SharedEventId const& id) {
  for (auto const& existing : peer.pending) {
    if (existing == id) {
      return;
    }
  }
  peer.pending.push_back(id);
}

bool SendEventToPeer(ChatSharedBinding& binding, PeerDeliveryState& peer,
                     SharedEventId const& id, ISharedTransport* transport) {
  if (transport == nullptr) {
    return false;
  }
  auto const* record = binding.instance.FindJournalRecord(id);
  if (record == nullptr || !record->event.is_valid()) {
    return false;
  }
  record->event.Load();
  auto payload = SerializeSharedEventPayload(*record->event);
  if (payload.empty()) {
    return false;
  }
  SharedEventFrame frame{
      .shared_room_id = binding.instance.shared_room_id,
      .event_id = record->identity,
      .order = record->order,
      .payload = std::move(payload),
  };
  transport->SendEvent(peer.remote_aether_uid, frame);
  chat::ChatLog("SHARED_EVENT_SEND peer=" + peer.remote_aether_uid +
                " event=" + id.origin_uid + ":" +
                std::to_string(id.origin_sequence));
  return true;
}

void StoreDeferred(ChatSharedBinding& binding, std::string const& source_peer_uid,
                   SharedEventFrame const& frame) {
  for (auto const& existing : binding.instance.deferred) {
    if (existing.id == frame.event_id) {
      return;
    }
  }
  binding.instance.deferred.push_back(DeferredIncomingEvent{
      .id = frame.event_id,
      .order = frame.order,
      .payload = frame.payload,
      .source_peer_uid = source_peer_uid,
  });
  chat::ChatLog("SHARED_EVENT_DEFERRED peer=" + source_peer_uid + " event=" +
                frame.event_id.origin_uid + ":" +
                std::to_string(frame.event_id.origin_sequence));
}

SharedApplyResult TryApplyFrame(
    ChatSharedBinding& binding, std::string const& source_peer_uid,
    SharedEventFrame const& frame,
    std::function<void(std::string const& client_uid)> const& on_join_client,
    OnNewChatClientFn const& on_new_client);

void DrainDeferred(
    ChatSharedBinding& binding,
    std::function<void(std::string const& client_uid)> const& on_join_client,
    OnNewChatClientFn const& on_new_client) {
  bool progressed = true;
  while (progressed) {
    progressed = false;
    for (std::size_t i = 0; i < binding.instance.deferred.size();) {
      auto frame = SharedEventFrame{
          .shared_room_id = binding.instance.shared_room_id,
          .event_id = binding.instance.deferred[i].id,
          .order = binding.instance.deferred[i].order,
          .payload = binding.instance.deferred[i].payload,
      };
      auto const source = binding.instance.deferred[i].source_peer_uid;
      auto const result =
          TryApplyFrame(binding, source, frame, on_join_client, on_new_client);
      if (result == SharedApplyResult::Applied ||
          result == SharedApplyResult::DuplicateAlreadyApplied) {
        binding.instance.deferred.erase(binding.instance.deferred.begin() +
                                        static_cast<std::ptrdiff_t>(i));
        progressed = true;
        continue;
      }
      if (result == SharedApplyResult::Rejected) {
        binding.instance.deferred.erase(binding.instance.deferred.begin() +
                                        static_cast<std::ptrdiff_t>(i));
        continue;
      }
      ++i;
    }
  }
}

SharedApplyResult TryApplyFrame(
    ChatSharedBinding& binding, std::string const& source_peer_uid,
    SharedEventFrame const& frame,
    std::function<void(std::string const& client_uid)> const& on_join_client,
    OnNewChatClientFn const& on_new_client) {
  if (binding.instance.HasSharedEvent(frame.event_id)) {
    return SharedApplyResult::DuplicateAlreadyApplied;
  }
  Event::ptr event;
  if (!DeserializeSharedEventPayload(
          *binding.instance.node, event, frame.payload,
          [&](ChatClient& client) {
            auto const uid = client.AetherUidText();
            if (auto remote = binding.presence.RemoteOnline(uid)) {
              client.online = *remote;
            } else if (uid == binding.instance.local_aether_uid) {
              if (auto local = binding.presence.LocalSelfOnline()) {
                client.online = *local;
              }
            }
            if (on_new_client) {
              on_new_client(client);
            }
          }) ||
      !event.is_valid()) {
    return SharedApplyResult::Rejected;
  }
  event.Load();
  auto* join = dynamic_cast<JoinEvent*>(&*event);
  if (join != nullptr) {
    if (join->client.is_valid()) {
      auto const client_uid = join->client->AetherUidText();
      if (!client_uid.empty() && client_uid != frame.event_id.origin_uid) {
        chat::ChatLog("SHARED_JOIN_UID_MISMATCH sender=" + source_peer_uid +
                      " event_uid=" + client_uid);
        return SharedApplyResult::Rejected;
      }
      if (client_uid.empty()) {
        join->client->SetAetherUidText(frame.event_id.origin_uid);
      }
    }
  }
  if (!event->CanApplyTo(*binding.instance.node)) {
    return SharedApplyResult::Deferred;
  }
  binding.instance.node->InsertSharedOrderedEvent(std::move(event),
                                                  frame.event_id, frame.order);
  binding.runtime.OnIncomingEventApplied(
      binding.instance, frame.event_id, frame.order, source_peer_uid,
      [](PeerDeliveryState& peer, SharedEventId const& event_id) {
        EnqueuePending(peer, event_id);
      });
  chat::ChatLog("SHARED_EVENT_APPLIED peer=" + source_peer_uid + " event=" +
                frame.event_id.origin_uid + ":" +
                std::to_string(frame.event_id.origin_sequence));
  if (join != nullptr && join->client.is_valid() && on_join_client) {
    on_join_client(join->client->AetherUidText());
  }
  return SharedApplyResult::Applied;
}

}  // namespace

void InitializeChatSharedBinding(ChatSharedBinding& binding, Application& app,
                                 std::string local_aether_uid) {
  binding.instance.node = app.chat_room;
  binding.instance.local_aether_uid = std::move(local_aether_uid);
  binding.instance.shared_room_id = binding.instance.local_aether_uid;
  if (app.host_client.is_valid() &&
      app.host_client->AetherUidText().empty()) {
    app.host_client->SetAetherUidText(binding.instance.local_aether_uid);
  }
  if (binding.instance.node.is_valid()) {
    for (auto const& record : binding.instance.node->journal) {
      if (!record.HasSharedIdentity()) {
        throw std::runtime_error(
            "ChatRoom journal contains pre-shared EventRecords without "
            "SharedEventId; re-distill with a fresh state dir");
      }
      binding.instance.RememberSharedEvent(record.identity);
      if (record.order.lamport > binding.instance.lamport_clock) {
        binding.instance.lamport_clock = record.order.lamport;
      }
      if (record.identity.origin_uid == binding.instance.local_aether_uid &&
          record.identity.origin_sequence >=
              binding.instance.next_origin_sequence) {
        binding.instance.next_origin_sequence =
            record.identity.origin_sequence + 1;
      }
    }
  }
}

void CommitLocalJoin(ChatSharedBinding& binding, ChatClient& client) {
  auto const id = binding.runtime.AssignLocalIdentity(binding.instance);
  auto const order = binding.runtime.MakeLocalOrder(binding.instance, id);
  auto event = MakeJoinEvent(*binding.instance.node, client);
  assert(binding.instance.node->CanApply(*event));
  binding.instance.node->CommitShared(event, id, order);
  binding.runtime.RememberLocalCommit(binding.instance, id);
  binding.runtime.OnLocalEventCommitted(
      binding.instance, id,
      [](PeerDeliveryState& peer, SharedEventId const& event_id) {
        EnqueuePending(peer, event_id);
        chat::ChatLog("SHARED_EVENT_PENDING peer=" + peer.remote_aether_uid +
                      " event=" + event_id.origin_uid + ":" +
                      std::to_string(event_id.origin_sequence));
      });
}

LocalChatCommitResult CommitLocalMessage(ChatSharedBinding& binding,
                                         ChatClient& author, std::string text,
                                         std::int64_t sent_at_unix_ms) {
  LocalChatCommitResult result;
  auto event = MakeChatMessageEvent(*binding.instance.node, author,
                                    std::move(text), sent_at_unix_ms);
  if (!event.is_valid()) {
    return result;
  }
  auto const id = binding.runtime.AssignLocalIdentity(binding.instance);
  auto const order = binding.runtime.MakeLocalOrder(binding.instance, id);
  assert(binding.instance.node->CanApply(*event));
  binding.instance.node->CommitShared(event, id, order);
  binding.runtime.RememberLocalCommit(binding.instance, id);
  binding.runtime.OnLocalEventCommitted(
      binding.instance, id,
      [](PeerDeliveryState& peer, SharedEventId const& event_id) {
        EnqueuePending(peer, event_id);
        chat::ChatLog("SHARED_EVENT_PENDING peer=" + peer.remote_aether_uid +
                      " event=" + event_id.origin_uid + ":" +
                      std::to_string(event_id.origin_sequence));
      });
  result.shared_event_id = id;
  result.local_event_obj_id = event.id().id();
  result.committed = true;
  return result;
}

std::vector<std::uint8_t> SerializeSharedEventPayload(Event const& event) {
  ByteSink sink;
  SerializeObjectGraphToBuffer(event, sink);
  return std::move(sink.bytes);
}

void StripRuntimeFieldsFromEventGraph(Event& event) {
  if (auto* join = dynamic_cast<JoinEvent*>(&event)) {
    if (join->client.is_valid()) {
      join->client->online = false;
    }
    return;
  }
  if (auto* message = dynamic_cast<ChatMessageEvent*>(&event)) {
    if (message->author.is_valid()) {
      message->author->online = false;
    }
  }
}

Event::ptr RemapIncomingEvent(ChatRoom& room, ae::Domain& model_domain,
                              std::vector<std::uint8_t> const& payload,
                              OnNewChatClientFn on_new_client) {
  if (payload.size() < sizeof(std::uint32_t)) {
    return {};
  }
  ByteSource peek{payload.data(), payload.size()};
  std::uint32_t layer_count = 0;
  peek.read(&layer_count, sizeof(layer_count));
  if (!peek.ok || layer_count == 0) {
    return {};
  }
  std::uint32_t root_oid = 0;
  std::uint32_t class_id = 0;
  bool found_event = false;
  for (std::uint32_t i = 0; i < layer_count; ++i) {
    std::uint32_t oid = 0;
    std::uint32_t cid = 0;
    std::uint8_t version = 0;
    std::uint32_t size = 0;
    peek.read(&oid, sizeof(oid));
    peek.read(&cid, sizeof(cid));
    peek.read(&version, sizeof(version));
    peek.read(&size, sizeof(size));
    if (!peek.ok || peek.pos + size > peek.size) {
      return {};
    }
    peek.pos += size;
    if (cid == JoinEvent::kClassId || cid == ChatMessageEvent::kClassId) {
      root_oid = oid;
      class_id = cid;
      found_event = true;
      break;
    }
  }
  if (!found_event) {
    return {};
  }
  ae::RamDomainStorage scratch_storage;
  ae::Domain scratch_domain{ae::Now(), scratch_storage};
  Event::ptr scratch_event;
  if (class_id == JoinEvent::kClassId) {
    scratch_event = JoinEvent::ptr::Create(
        ae::CreateWith{scratch_domain}.with_id(ae::ObjId{root_oid}));
  } else if (class_id == ChatMessageEvent::kClassId) {
    scratch_event = ChatMessageEvent::ptr::Create(
        ae::CreateWith{scratch_domain}.with_id(ae::ObjId{root_oid}));
  } else {
    return {};
  }
  ByteSource source{payload.data(), payload.size()};
  DeserializeObjectGraphFromBuffer(*scratch_event, source, scratch_domain,
                                   scratch_storage);
  scratch_event.Load();
  if (auto* join = dynamic_cast<JoinEvent*>(&*scratch_event)) {
    if (!join->client.is_valid()) {
      return {};
    }
    join->client.Load();
    auto event = JoinEvent::ptr::Create(ae::CreateWith{model_domain});
    auto const uid = join->client->AetherUidText();
    auto const name = join->client->DisplayNameBytes();
    if (auto existing = room.FindClientByAetherUid(uid); existing.is_valid()) {
      event->client = existing;
    } else {
      auto client = ChatClient::ptr::Create(ae::CreateWith{model_domain});
      client->SetAetherUidText(uid);
      auto name_obj =
          ImmutableString::ptr::Create(ae::CreateWith{model_domain});
      name_obj->bytes = name;
      client->display_name = name_obj;
      if (on_new_client) {
        on_new_client(*client);
      }
      event->client = client;
    }
    return event;
  }
  if (auto* message = dynamic_cast<ChatMessageEvent*>(&*scratch_event)) {
    if (!message->author.is_valid() || !message->text.is_valid()) {
      return {};
    }
    message->author.Load();
    message->text.Load();
    auto event = ChatMessageEvent::ptr::Create(ae::CreateWith{model_domain});
    auto const author_uid = message->author->AetherUidText();
    event->author = room.FindClientByAetherUid(author_uid);
    if (!event->author.is_valid()) {
      // Author not in room yet (Message before Join): build a temporary
      // client so CanApply fails and ApplyIncomingSharedEvent Defers.
      auto client = ChatClient::ptr::Create(ae::CreateWith{model_domain});
      client->SetAetherUidText(author_uid);
      auto name_obj =
          ImmutableString::ptr::Create(ae::CreateWith{model_domain});
      name_obj->bytes = message->author->DisplayNameBytes();
      client->display_name = name_obj;
      event->author = client;
    }
    auto body = ImmutableString::ptr::Create(ae::CreateWith{model_domain});
    body->bytes = message->text->bytes;
    event->text = body;
    event->sent_at_unix_ms = message->sent_at_unix_ms;
    return event;
  }
  return {};
}

bool DeserializeSharedEventPayload(ChatRoom& room, Event::ptr& out_event,
                                   std::vector<std::uint8_t> const& payload,
                                   OnNewChatClientFn on_new_client) {
  out_event = RemapIncomingEvent(room, *room.domain, payload,
                                 std::move(on_new_client));
  return out_event.is_valid();
}

SharedApplyResult ApplyIncomingSharedEvent(
    ChatSharedBinding& binding, std::string const& source_peer_uid,
    SharedEventFrame const& frame,
    std::function<void(std::string const& client_uid)> on_join_client,
    OnNewChatClientFn on_new_client) {
  chat::ChatLog("SHARED_EVENT_RECEIVED peer=" + source_peer_uid + " event=" +
                frame.event_id.origin_uid + ":" +
                std::to_string(frame.event_id.origin_sequence));
  if (frame.shared_room_id != binding.instance.shared_room_id &&
      !binding.instance.shared_room_id.empty() &&
      frame.shared_room_id != binding.instance.local_aether_uid) {
    binding.instance.shared_room_id = frame.shared_room_id;
  }
  if (binding.instance.HasSharedEvent(frame.event_id)) {
    chat::ChatLog("SHARED_EVENT_DUPLICATE peer=" + source_peer_uid +
                  " event=" + frame.event_id.origin_uid + ":" +
                  std::to_string(frame.event_id.origin_sequence));
    return SharedApplyResult::DuplicateAlreadyApplied;
  }
  auto const result =
      TryApplyFrame(binding, source_peer_uid, frame, on_join_client,
                    on_new_client);
  if (result == SharedApplyResult::Deferred) {
    StoreDeferred(binding, source_peer_uid, frame);
    return SharedApplyResult::Deferred;
  }
  if (result == SharedApplyResult::Applied) {
    DrainDeferred(binding, on_join_client, on_new_client);
    ApplyPresenceOverlay(binding);
  }
  return result;
}

void EnsureSharedPeer(ChatSharedBinding& binding,
                      std::string const& remote_uid) {
  if (remote_uid.empty() || remote_uid == binding.instance.local_aether_uid) {
    return;
  }
  auto& peer = binding.runtime.EnsurePeer(binding.instance, remote_uid);
  binding.runtime.SeedPendingFromJournal(binding.instance, peer);
}

void ConnectToHostCommand(ChatSharedBinding& binding, std::string host_uid,
                          OpenPeerRequestFn request_open_peer) {
  while (!host_uid.empty() &&
         (host_uid.back() == '\n' || host_uid.back() == '\r' ||
          host_uid.back() == ' ' || host_uid.back() == '\t')) {
    host_uid.pop_back();
  }
  std::size_t start = 0;
  while (start < host_uid.size() &&
         (host_uid[start] == ' ' || host_uid[start] == '\t')) {
    ++start;
  }
  if (start > 0) {
    host_uid.erase(0, start);
  }
  if (host_uid.empty() || host_uid == binding.instance.local_aether_uid) {
    return;
  }
  chat::ChatLog("SHARED_CONNECT_REQUEST peer=" + host_uid);
  binding.instance.shared_room_id = host_uid;
  EnsureSharedPeer(binding, host_uid);
  if (request_open_peer) {
    chat::ChatLog("SHARED_STREAM_OPENING peer=" + host_uid);
    request_open_peer(host_uid);
  }
}

void SetSharedPeerChannelReady(ChatSharedBinding& binding,
                               std::string const& remote_uid, bool ready) {
  EnsureSharedPeer(binding, remote_uid);
  auto* peer = binding.instance.FindPeer(remote_uid);
  if (peer == nullptr) {
    return;
  }
  peer->channel_ready = ready;
  if (ready) {
    chat::ChatLog("SHARED_STREAM_READY peer=" + remote_uid);
  } else {
    chat::ChatLog("SHARED_STREAM_CLOSED peer=" + remote_uid);
  }
}

void RequeueInFlightAfterWriteFailed(ChatSharedBinding& binding,
                                     std::string const& remote_uid) {
  auto* peer = binding.instance.FindPeer(remote_uid);
  if (peer == nullptr) {
    return;
  }
  for (auto const& entry : peer->in_flight) {
    EnqueuePending(*peer, entry.id);
  }
  peer->in_flight.clear();
  chat::ChatLog("SHARED_WRITE_FAILED peer=" + remote_uid +
                " requeued_pending=" + std::to_string(peer->pending.size()));
}

void SetSharedPeerOnline(ChatSharedBinding& binding,
                         std::string const& remote_uid, bool online) {
  EnsureSharedPeer(binding, remote_uid);
  auto* peer = binding.instance.FindPeer(remote_uid);
  if (peer == nullptr) {
    return;
  }
  chat::ChatLog(std::string{"REMOTE_PRESENCE peer="} + remote_uid +
                " online=" + (online ? "1" : "0"));
  if (remote_uid == binding.instance.local_aether_uid) {
    binding.presence.SetLocalSelfOnline(online);
  } else {
    binding.presence.SetRemoteOnline(remote_uid, online);
  }
  ApplyPresenceOverlay(binding);
}

void ApplyPresenceOverlay(ChatSharedBinding& binding) {
  if (!binding.instance.node.is_valid()) {
    return;
  }
  binding.presence.ApplyToRoom(*binding.instance.node,
                               binding.instance.local_aether_uid);
}

void HandleSharedAck(ChatSharedBinding& binding,
                     std::string const& from_peer_uid,
                     SharedAckFrame const& frame) {
  binding.runtime.OnAckReceived(binding.instance, from_peer_uid, frame.event_id);
  chat::ChatLog("SHARED_ACK_RECEIVED peer=" + from_peer_uid + " event=" +
                frame.event_id.origin_uid + ":" +
                std::to_string(frame.event_id.origin_sequence));
}

void SendSharedAck(ChatSharedBinding& binding, ISharedTransport* transport,
                   std::string const& peer_uid, SharedEventId const& event_id) {
  if (transport == nullptr) {
    return;
  }
  SharedAckFrame ack{.shared_room_id = binding.instance.shared_room_id,
                     .event_id = event_id};
  transport->SendAck(peer_uid, ack);
  chat::ChatLog("SHARED_ACK_SEND peer=" + peer_uid + " event=" +
                event_id.origin_uid + ":" +
                std::to_string(event_id.origin_sequence));
}

void TickSharedDelivery(ChatSharedBinding& binding,
                        std::chrono::steady_clock::time_point now,
                        ISharedTransport* transport) {
  binding.runtime.Tick(
      binding.instance, now,
      [&](PeerDeliveryState& peer, SharedEventId const& id) {
        return SendEventToPeer(binding, peer, id, transport);
      });
}

}  // namespace apptraverse
