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

bool ChatRoom::CanApply(JoinEvent const& event) const {
  return event.client.is_valid() && !HasClient(event.client.id().id());
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
  feed.push_back(item);
  NoteMaterializedChange();
}

}  // namespace apptraverse
