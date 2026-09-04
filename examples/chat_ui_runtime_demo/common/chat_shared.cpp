#include "chat_shared.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <stdexcept>
#include <utility>

#include "aether-objects/domain_storage/ram_domain_storage.h"

#include "apptraverse/object_serialization.h"
#include "apptraverse/shared_event_order.h"
#include "apptraverse/shared_frame_codec.h"

#include "chat_commands.h"
#include "chat_events.h"
#include "chat_log.h"
#include "chat_presence.h"

namespace apptraverse {
namespace {

ChatClient::ptr FindRoomClientForIncomingAuthor(
    ChatRoom const& room, ChatClient const& scratch_author,
    std::string const& fallback_author_uid) {
  auto const author_uid = scratch_author.AetherUidText();
  if (!author_uid.empty()) {
    if (auto by_uid = room.FindClientByAetherUid(author_uid); by_uid.is_valid()) {
      return by_uid;
    }
  }
  if (!fallback_author_uid.empty()) {
    if (auto by_uid = room.FindClientByAetherUid(fallback_author_uid);
        by_uid.is_valid()) {
      return by_uid;
    }
  }
  auto const display_name = scratch_author.DisplayNameBytes();
  if (!display_name.empty()) {
    for (auto const& client : room.clients) {
      if (client.is_valid() && client->DisplayNameBytes() == display_name) {
        return client;
      }
    }
  }
  return {};
}

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
            // Seed presentation cache from runtime overlay only — never from
            // the imported shared payload Presence field.
            if (uid == binding.instance.local_aether_uid) {
              client.SetPresence(binding.presence.LocalSelf());
            } else if (!uid.empty()) {
              client.SetPresence(binding.presence.Remote(uid));
            } else {
              client.SetPresence(PresenceState::kUnknown);
            }
            if (on_new_client) {
              on_new_client(client);
            }
          },
          frame.event_id.origin_uid) ||
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
      join->client->SetPresence(PresenceState::kUnknown);
    }
    return;
  }
  if (auto* message = dynamic_cast<ChatMessageEvent*>(&event)) {
    if (message->author.is_valid()) {
      message->author->SetPresence(PresenceState::kUnknown);
    }
  }
}

Event::ptr RemapIncomingEvent(ChatRoom& room, ae::Domain& model_domain,
                              std::vector<std::uint8_t> const& payload,
                              OnNewChatClientFn on_new_client,
                              std::string fallback_author_uid) {
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
  ae::Domain scratch_domain{scratch_storage};
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
    ae::DomainGraph scratch_graph{&scratch_domain};
    if (message->author.is_valid()) {
      scratch_graph.LoadRoot(message->author.id());
    }
    if (message->text.is_valid()) {
      scratch_graph.LoadRoot(message->text.id());
    }
    message->author.Load();
    message->text.Load();
    std::string text_bytes;
    if (message->text.is_valid()) {
      if (auto copy = scratch_graph.LoadCopy<ImmutableString>(
              message->text.id(), ae::ObjId::GenerateUnique())) {
        text_bytes = copy->bytes;
      }
      if (text_bytes.empty()) {
        text_bytes = message->text->bytes;
      }
    }
    if (text_bytes.empty()) {
      auto const author_display = message->author->DisplayNameBytes();
      auto const scratch_author_uid = message->author->AetherUidText();
      for (auto const& [obj_id, class_map_opt] : scratch_storage.state) {
        if (!class_map_opt) {
          continue;
        }
        if (class_map_opt->find(ImmutableString::kClassId) ==
            class_map_opt->end()) {
          continue;
        }
        if (auto copy = scratch_graph.LoadCopy<ImmutableString>(
                obj_id, ae::ObjId::GenerateUnique())) {
          if (copy->bytes.empty() || copy->bytes == author_display ||
              copy->bytes == scratch_author_uid) {
            continue;
          }
          text_bytes = copy->bytes;
          break;
        }
      }
    }
    auto event = ChatMessageEvent::ptr::Create(ae::CreateWith{model_domain});
    auto const author_uid = message->author->AetherUidText();
    event->author = FindRoomClientForIncomingAuthor(
        room, *message->author, fallback_author_uid);
    if (!event->author.is_valid()) {
      // Author not in room yet (Message before Join): build a temporary
      // client so CanApply fails and ApplyIncomingSharedEvent Defers.
      auto client = ChatClient::ptr::Create(ae::CreateWith{model_domain});
      client->SetAetherUidText(
          author_uid.empty() ? fallback_author_uid : author_uid);
      auto name_obj =
          ImmutableString::ptr::Create(ae::CreateWith{model_domain});
      name_obj->bytes = message->author->DisplayNameBytes();
      client->display_name = name_obj;
      event->author = client;
    }
    auto body = ImmutableString::ptr::Create(ae::CreateWith{model_domain});
    body->bytes = std::move(text_bytes);
    event->text = body;
    event->sent_at_unix_ms = message->sent_at_unix_ms;
    return event;
  }
  return {};
}

bool DeserializeSharedEventPayload(ChatRoom& room, Event::ptr& out_event,
                                   std::vector<std::uint8_t> const& payload,
                                   OnNewChatClientFn on_new_client,
                                   std::string fallback_author_uid) {
  out_event = RemapIncomingEvent(room, *room.domain, payload,
                                 std::move(on_new_client),
                                 std::move(fallback_author_uid));
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

ChatClient::ptr EnsurePresenceContact(ChatRoom& room,
                                      std::string const& remote_uid) {
  if (remote_uid.empty()) {
    return {};
  }
  if (auto existing = room.FindClientByAetherUid(remote_uid);
      existing.is_valid()) {
    return existing;
  }
  auto& domain = *room.domain;
  auto client = ChatClient::ptr::Create(ae::CreateWith{domain});
  client->SetAetherUidText(remote_uid);
  auto name = ImmutableString::ptr::Create(ae::CreateWith{domain});
  name->bytes =
      remote_uid.size() > 8 ? remote_uid.substr(0, 8) : remote_uid;
  client->display_name = name;
  client->SetPresence(PresenceState::kUnknown);
  room.clients.push_back(client);
  room.NotifyPresentationChanged();
  chat::ChatLog("PRESENCE_CONTACT_ENSURED uid=" + remote_uid);
  return client;
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

std::size_t ApplyPresenceOverlay(ChatSharedBinding& binding) {
  if (!binding.instance.node.is_valid()) {
    return 0;
  }
  return binding.presence.ApplyToRoom(*binding.instance.node,
                                      binding.instance.local_aether_uid);
}

bool SetRemotePresenceObservation(ChatSharedBinding& binding,
                                  std::string const& remote_uid,
                                  PresenceState state) {
  chat::ChatLog(std::string{"REMOTE_PRESENCE peer="} + remote_uid +
                " state=" + PresenceStateName(state));
  bool overlay_changed = false;
  if (remote_uid == binding.instance.local_aether_uid) {
    overlay_changed = binding.presence.SetLocalSelf(state);
  } else {
    overlay_changed = binding.presence.SetRemote(remote_uid, state);
  }
  auto const applied = ApplyPresenceOverlay(binding);
  return overlay_changed || applied > 0;
}

bool SetLocalPresenceObservation(ChatSharedBinding& binding,
                                 PresenceState state) {
  chat::ChatLog(std::string{"LOCAL_PRESENCE_APPLY state="} +
                PresenceStateName(state));
  bool const overlay_changed = binding.presence.SetLocalSelf(state);
  auto const applied = ApplyPresenceOverlay(binding);
  return overlay_changed || applied > 0;
}

void SetSharedPeerPresence(ChatSharedBinding& binding,
                           std::string const& remote_uid, PresenceState state) {
  // Legacy name retained for older call sites; Presence must not seed peers.
  static_cast<void>(SetRemotePresenceObservation(binding, remote_uid, state));
}

std::size_t CountSharedPendingAndInFlight(ChatSharedBinding const& binding) {
  std::size_t total = 0;
  for (auto const& peer : binding.instance.peers) {
    total += peer.pending.size();
    total += peer.in_flight.size();
  }
  return total;
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
