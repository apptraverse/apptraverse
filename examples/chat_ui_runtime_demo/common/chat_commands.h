#ifndef APPTRAVERSE_CHAT_COMMANDS_H_
#define APPTRAVERSE_CHAT_COMMANDS_H_

#include <cassert>
#include <cstdint>
#include <string>

#include "chat_events.h"
#include "chat_model.h"
#include "chat_presentation.h"

namespace apptraverse {

struct ChatSendUiRequest {
  std::string text;
  std::int64_t sent_at_unix_ms{0};
  std::uint64_t ui_trace_id{0};
};

inline void CommitJoinChat(ChatRoom& room, ChatClient& client) {
  auto event = JoinEvent::ptr::Create(ae::CreateWith{*room.domain});
  event->client = ChatClient::ptr::MakeFromThis(&client);
  assert(room.CanApply(*event));
  room.Commit(event);
}

inline ChatMessageEvent::ptr CommitSendChatMessage(
    ChatRoom& room, ChatClient& author, std::string text,
    std::int64_t sent_at_unix_ms = 0) {
  while (!text.empty() &&
         (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' ||
          text.back() == '\t')) {
    text.pop_back();
  }
  std::size_t start = 0;
  while (start < text.size() &&
         (text[start] == ' ' || text[start] == '\t' || text[start] == '\n' ||
          text[start] == '\r')) {
    ++start;
  }
  if (start > 0) {
    text.erase(0, start);
  }
  if (text.empty()) {
    return {};
  }
  auto body = ImmutableString::ptr::Create(ae::CreateWith{*room.domain});
  body->bytes = std::move(text);
  auto event = ChatMessageEvent::ptr::Create(ae::CreateWith{*room.domain});
  event->author = ChatClient::ptr::MakeFromThis(&author);
  event->text = body;
  event->sent_at_unix_ms = sent_at_unix_ms;
  assert(room.CanApply(*event));
  room.Commit(event);
  return event;
}

inline std::string FormatChatFeedLine(ChatFeedItem const& item) {
  std::string name;
  if (item.client.is_valid()) {
    item.client.Load();
    name = item.client->DisplayNameBytes();
  }
  if (item.kind == kChatFeedKindJoin) {
    return FormatChatJoinDisplayLine(name);
  }
  std::string body;
  if (item.body.is_valid()) {
    item.body.Load();
    assert(item.body.is_loaded());
    body = item.body->bytes;
  }
  return FormatChatMessageDisplayLine(name, body, item.sent_at_unix_ms);
}

inline void SetApplicationRole(Application& application, ChatRole role) {
  if (application.GetRole() == role) {
    return;
  }
  application.SetRole(role);
}

inline void SetLocalAetherUidText(LocalAetherIdentity& identity,
                                 std::string uid_text) {
  if (identity.UidTextBytes() == uid_text) {
    return;
  }
  identity.SetUidTextBytes(std::move(uid_text));
}

inline void SetHostClientOnline(ChatClient& client, bool online) {
  client.SetOnline(online);
}

inline void ResetRuntimePresenceState(Application& application) {
  if (application.host_client.is_valid()) {
    application.host_client->online = false;
  }
  if (application.chat_room.is_valid()) {
    for (auto const& client : application.chat_room->clients) {
      if (client.is_valid()) {
        client->online = false;
      }
    }
  }
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_CHAT_COMMANDS_H_
