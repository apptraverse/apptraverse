#include "chat_shared.h"

#include <algorithm>
#include <cassert>
#include <cstring>

#include "aether/domain_storage/ram_domain_storage.h"

#include "apptraverse/object_serialization.h"
#include "apptraverse/publication_channel.h"
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

std::size_t FindInsertIndex(std::vector<SharedJournalEntry> const& journal,
                            SharedEventOrder const& order) {
  auto it = std::lower_bound(
      journal.begin(), journal.end(), order,
      [](SharedJournalEntry const& entry, SharedEventOrder const& value) {
        return SharedEventOrderLess(entry.order, value);
      });
  return static_cast<std::size_t>(it - journal.begin());
}

bool SendEventToPeer(ChatSharedBinding& binding, PeerDeliveryState& peer,
                     SharedEventId const& id, ISharedTransport* transport) {
  if (transport == nullptr) {
    return false;
  }
  SharedJournalEntry const* entry = nullptr;
  for (auto const& candidate : binding.instance.shared_journal) {
    if (candidate.id == id) {
      entry = &candidate;
      break;
    }
  }
  if (entry == nullptr) {
    return false;
  }
  std::size_t const journal_index = [&]() {
    for (std::size_t i = 0; i < binding.instance.shared_journal.size(); ++i) {
      if (binding.instance.shared_journal[i].id == id) {
        return i;
      }
    }
    return binding.instance.node->journal.size();
  }();
  if (journal_index >= binding.instance.node->journal.size()) {
    return false;
  }
  auto const& record = binding.instance.node->journal[journal_index];
  assert(record.event.is_valid());
  record.event.Load();
  SharedEventFrame frame{
      .shared_room_id = binding.instance.shared_room_id,
      .event_id = entry->id,
      .order = entry->order,
      .payload = SerializeSharedEventPayload(*record.event),
  };
  transport->SendEvent(frame);
  return true;
}

}  // namespace

void InitializeChatSharedBinding(ChatSharedBinding& binding,
                                 Application& app,
                                 std::string local_aether_uid) {
  binding.instance.node = app.chat_room;
  binding.instance.local_aether_uid = std::move(local_aether_uid);
  binding.instance.shared_room_id = binding.instance.local_aether_uid;
  if (app.host_client.is_valid() &&
      app.host_client->AetherUidText().empty()) {
    app.host_client->SetAetherUidText(binding.instance.local_aether_uid);
  }
  if (binding.instance.shared_journal.empty() &&
      binding.instance.node.is_valid()) {
    std::uint64_t seq = 1;
    for (auto const& record : binding.instance.node->journal) {
      (void)record;
      SharedEventId id{.origin_uid = binding.instance.local_aether_uid,
                       .origin_sequence = seq};
      SharedEventOrder order{.lamport = seq,
                             .origin_uid = binding.instance.local_aether_uid,
                             .origin_sequence = seq};
      binding.instance.RememberSharedEvent(id);
      binding.instance.shared_journal.push_back(
          SharedJournalEntry{.id = id, .order = order});
      ++seq;
    }
    binding.instance.next_origin_sequence = seq;
    binding.instance.lamport_clock = seq;
  }
}

void CommitLocalJoin(ChatSharedBinding& binding, ChatClient& client) {
  auto const id = binding.runtime.AssignLocalIdentity(binding.instance);
  auto const order = binding.runtime.MakeLocalOrder(binding.instance, id);
  CommitJoinChat(*binding.instance.node, client);
  binding.runtime.RememberLocalCommit(binding.instance, id, order);
  binding.runtime.OnLocalEventCommitted(
      binding.instance, id,
      [](PeerDeliveryState& peer, SharedEventId const& event_id) {
        EnqueuePending(peer, event_id);
      });
}

void CommitLocalMessage(ChatSharedBinding& binding, ChatClient& author,
                        std::string text) {
  auto const id = binding.runtime.AssignLocalIdentity(binding.instance);
  auto const order = binding.runtime.MakeLocalOrder(binding.instance, id);
  CommitSendChatMessage(*binding.instance.node, author, std::move(text));
  binding.runtime.RememberLocalCommit(binding.instance, id, order);
  binding.runtime.OnLocalEventCommitted(
      binding.instance, id,
      [](PeerDeliveryState& peer, SharedEventId const& event_id) {
        EnqueuePending(peer, event_id);
      });
}

std::vector<std::uint8_t> SerializeSharedEventPayload(Event const& event) {
  std::vector<std::uint8_t> bytes;
  ByteSink sink{bytes};
  SerializeObjectGraphToBuffer(event, sink);
  return bytes;
}

bool DeserializeSharedEventPayload(ae::Domain& domain, Event::ptr& out_event,
                                   std::vector<std::uint8_t> const& payload) {
  (void)domain;
  out_event = {};
  (void)payload;
  return false;
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
                              std::vector<std::uint8_t> const& payload) {
  if (payload.size() < sizeof(std::uint32_t) * 2) {
    return {};
  }
  std::size_t pos = sizeof(std::uint32_t);
  std::uint32_t root_oid = 0;
  std::uint32_t class_id = 0;
  std::memcpy(&root_oid, payload.data() + pos, sizeof(root_oid));
  pos += sizeof(root_oid);
  std::memcpy(&class_id, payload.data() + pos, sizeof(class_id));
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
      event->client = client;
    }
    return event;
  }
  if (auto* message = dynamic_cast<ChatMessageEvent*>(&*scratch_event)) {
    auto event = ChatMessageEvent::ptr::Create(ae::CreateWith{model_domain});
    auto const author_uid = message->author->AetherUidText();
    event->author = room.FindClientByAetherUid(author_uid);
    if (!event->author.is_valid()) {
      return {};
    }
    auto body =
        ImmutableString::ptr::Create(ae::CreateWith{model_domain});
    body->bytes = message->text->bytes;
    event->text = body;
    return event;
  }
  return {};
}

bool ApplyIncomingSharedEvent(
    ChatSharedBinding& binding, std::string const& source_peer_uid,
    SharedEventFrame const& frame,
    std::function<void(std::string const& client_uid)> on_join_client) {
  if (frame.shared_room_id != binding.instance.shared_room_id &&
      !binding.instance.shared_room_id.empty()) {
    binding.instance.shared_room_id = frame.shared_room_id;
  }
  if (binding.instance.HasSharedEvent(frame.event_id)) {
    chat::ChatLog("SHARED_EVENT_DUPLICATE event_id=" + frame.event_id.origin_uid +
                  ":" + std::to_string(frame.event_id.origin_sequence));
    return true;
  }
  Event::ptr event = RemapIncomingEvent(*binding.instance.node,
                                          *binding.instance.node->domain,
                                          frame.payload);
  if (!event.is_valid()) {
    return false;
  }
  event.Load();
  auto* join = dynamic_cast<JoinEvent*>(&*event);
  if (join != nullptr) {
    if (join->client.is_valid()) {
      auto const client_uid = join->client->AetherUidText();
      if (!client_uid.empty() && client_uid != frame.event_id.origin_uid) {
        chat::ChatLog("SHARED_JOIN_UID_MISMATCH sender=" +
                      source_peer_uid + " event_uid=" + client_uid);
        return false;
      }
      if (client_uid.empty()) {
        join->client->SetAetherUidText(frame.event_id.origin_uid);
      }
    }
  }
  if (!event->CanApplyTo(*binding.instance.node)) {
    binding.instance.RememberSharedEvent(frame.event_id);
    binding.runtime.OnIncomingEventApplied(
        binding.instance, frame.event_id, frame.order, source_peer_uid,
        [](PeerDeliveryState& peer, SharedEventId const& event_id) {
          EnqueuePending(peer, event_id);
        });
    return true;
  }
  auto const insert_index =
      FindInsertIndex(binding.instance.shared_journal, frame.order);
  auto const timestamp = EncodeOrderTimestamp(frame.order);
  binding.instance.node->InsertOrderedEvent(std::move(event), timestamp);
  binding.instance.shared_journal.insert(
      binding.instance.shared_journal.begin() +
          static_cast<std::ptrdiff_t>(insert_index),
      SharedJournalEntry{.id = frame.event_id, .order = frame.order});
  binding.runtime.OnIncomingEventApplied(
      binding.instance, frame.event_id, frame.order, source_peer_uid,
      [](PeerDeliveryState& peer, SharedEventId const& event_id) {
        EnqueuePending(peer, event_id);
      });
  if (join != nullptr && join->client.is_valid() && on_join_client) {
    on_join_client(join->client->AetherUidText());
  }
  return true;
}

void EnsureSharedPeer(ChatSharedBinding& binding, std::string const& remote_uid,
                      ISharedTransport* transport) {
  if (remote_uid.empty() || remote_uid == binding.instance.local_aether_uid) {
    return;
  }
  auto& peer = binding.runtime.EnsurePeer(binding.instance, remote_uid);
  binding.runtime.SeedPendingFromJournal(binding.instance, peer);
  peer.online = true;
  chat::ChatLog("SHARED_STREAM_READY peer_uid=" + remote_uid);
  (void)transport;
}

void HandleSharedAck(ChatSharedBinding& binding,
                     std::string const& from_peer_uid,
                     SharedAckFrame const& frame) {
  binding.runtime.OnAckReceived(binding.instance, from_peer_uid, frame.event_id);
  chat::ChatLog("SHARED_ACK_RECEIVED peer_uid=" + from_peer_uid + " event_id=" +
                frame.event_id.origin_uid + ":" +
                std::to_string(frame.event_id.origin_sequence));
}

void TickSharedDelivery(
    ChatSharedBinding& binding, std::chrono::steady_clock::time_point now,
    std::function<ISharedTransport*(std::string const& peer_uid)> const&
        transport_for_peer) {
  binding.runtime.Tick(
      binding.instance, now,
      [&](PeerDeliveryState& peer, SharedEventId const& id) {
        return SendEventToPeer(binding, peer, id,
                               transport_for_peer(peer.remote_aether_uid));
      });
}

}  // namespace apptraverse
