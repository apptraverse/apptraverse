#include "chat_model.h"

#include <cassert>

#include "chat_events.h"

namespace apptraverse {

bool ChatRoom::HasClient(std::uint32_t client_id) const {
  for (auto const& client : clients) {
    if (client.is_valid() && client.id().id() == client_id) {
      return true;
    }
  }
  return false;
}

bool ChatRoom::HasClientByAetherUid(std::string const& uid) const {
  if (uid.empty()) {
    return false;
  }
  for (auto const& client : clients) {
    if (client.is_valid() && client->AetherUidText() == uid) {
      return true;
    }
  }
  return false;
}

ChatClient::ptr ChatRoom::FindClientByAetherUid(std::string const& uid) const {
  for (auto const& client : clients) {
    if (client.is_valid() && client->AetherUidText() == uid) {
      return client;
    }
  }
  return {};
}

bool ChatRoom::CanApply(JoinEvent const& event) const {
  if (!event.client.is_valid()) {
    return false;
  }
  auto const uid = event.client->AetherUidText();
  if (!uid.empty() && HasClientByAetherUid(uid)) {
    return false;
  }
  return !HasClient(event.client.id().id());
}

bool ChatRoom::CanApply(ChatMessageEvent const& event) const {
  return event.author.is_valid() && event.text.is_valid() &&
         HasClient(event.author.id().id());
}

void ChatRoom::Apply(JoinEvent const& event) {
  assert(CanApply(event));
  clients.push_back(event.client);
  auto item = ChatFeedItem::ptr::Create(ae::CreateWith{*domain});
  item->kind = kChatFeedKindJoin;
  item->client = event.client;
  feed.push_back(item);
  NoteMaterializedChange();
}

void ChatRoom::Apply(ChatMessageEvent const& event) {
  assert(CanApply(event));
  auto item = ChatFeedItem::ptr::Create(ae::CreateWith{*domain});
  item->kind = kChatFeedKindMessage;
  item->client = event.author;
  item->body = event.text;
  item->sent_at_unix_ms = event.sent_at_unix_ms;
  item->source_event_obj_id = event.obj_id.id();
  feed.push_back(item);
  NoteMaterializedChange();
}

bool ChatClient::CanApply(LocalPresenceEvent const& event) const {
  return GetPresence() != event.GetPresence();
}

void ChatClient::Apply(LocalPresenceEvent const& event) {
  assert(CanApply(event));
  SetPresence(event.GetPresence());
}

}  // namespace apptraverse
