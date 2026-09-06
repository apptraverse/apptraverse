#ifndef CHAT_COMMANDS_H_
#define CHAT_COMMANDS_H_

#include <cassert>
#include <cstdint>
#include <string>

#include "apptraverse/distill.h"
#include "apptraverse/model_runtime.h"
#include "apptraverse/runtime_lifecycle.h"
#include "apptraverse/runtime_node.h"

#include "chat_events.h"
#include "chat_model.h"
#include "chat_presence.h"
#include "chat_presentation.h"

namespace chat {

struct ChatSendUiRequest {
  std::string text;
  std::int64_t sent_at_unix_ms{0};
  std::uint64_t ui_trace_id{0};
};

inline ClientAddedEvent::ptr MakeClientAddedEvent(ChatRoom& room,
                                                  ChatClient& client) {
  auto event = ClientAddedEvent::ptr::Create(ae::CreateWith{*room.domain});
  event->client = ChatClient::ptr::MakeFromThis(&client);
  return event;
}

inline void CommitClientAdded(ChatRoom& room, ChatClient& client) {
  auto event = MakeClientAddedEvent(room, client);
  assert(room.CanApply(*event));
  room.Commit(event);
}

inline std::string TrimChatMessageText(std::string text) {
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
  return text;
}

inline ChatMessageEvent::ptr MakeChatMessageEvent(
    ChatRoom& room, ChatClient& author, std::string text,
    std::int64_t sent_at_unix_ms = 0) {
  text = TrimChatMessageText(std::move(text));
  if (text.empty()) {
    return {};
  }
  auto body = ImmutableString::ptr::Create(ae::CreateWith{*room.domain});
  body->bytes = std::move(text);
  auto event = ChatMessageEvent::ptr::Create(ae::CreateWith{*room.domain});
  event->author = ChatClient::ptr::MakeFromThis(&author);
  event->text = body;
  event->sent_at_unix_ms = sent_at_unix_ms;
  return event;
}

inline ChatMessageEvent::ptr CommitSendChatMessage(
    ChatRoom& room, ChatClient& author, std::string text,
    std::int64_t sent_at_unix_ms = 0) {
  auto event =
      MakeChatMessageEvent(room, author, std::move(text), sent_at_unix_ms);
  if (!event.is_valid()) {
    return {};
  }
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

inline void SetApplicationRole(ChatApplication& application, ChatRole role) {
  if (application.GetRole() == role) {
    return;
  }
  application.SetRole(role);
}

// Creates the local ChatClient and stores it on ChatApplication. Does not add
// it to ChatRoom.clients.
inline ChatClient::ptr CreateUnjoinedLocalClient(ChatApplication& application,
                                                 std::string uid) {
  if (application.local_client.is_valid()) {
    if (!uid.empty() && application.local_client->AetherUidText().empty()) {
      application.local_client->SetAetherUidText(std::move(uid));
    }
    return application.local_client;
  }
  assert(application.domain != nullptr);
  auto client = ChatClient::ptr::Create(ae::CreateWith{*application.domain});
  auto name = ImmutableString::ptr::Create(ae::CreateWith{*application.domain});
  name->bytes = application.LocalDisplayNameBytes();
  client->display_name = name;
  if (!uid.empty()) {
    auto uid_obj =
        ImmutableString::ptr::Create(ae::CreateWith{*application.domain});
    uid_obj->bytes = std::move(uid);
    client->aether_uid = uid_obj;
  }
  apptraverse::InitializeRuntimeNode(*client);
  application.local_client = client;
  ChatApplication::ptr::MakeFromThis(&application)
      .Save();  // runtime-save-ok: bind local client
  return client;
}

inline bool CommitPresenceChanged(ChatClient& client, PresenceState state) {
  if (client.GetPresence() == state) {
    return false;
  }
  auto event = PresenceChangedEvent::ptr::Create(ae::CreateWith{*client.domain});
  event->SetPresence(state);
  if (!client.CanApply(*event)) {
    return false;
  }
  client.Commit(event);
  return true;
}

inline bool CommitPresenceMonitoringStarted(ChatClient& client) {
  auto event = PresenceMonitoringStartedEvent::ptr::Create(
      ae::CreateWith{*client.domain});
  if (!client.CanApply(*event)) {
    return false;
  }
  client.Commit(event);
  return true;
}

inline void BeginCurrentRun(ChatApplication& application) {
  assert(application.runtime.is_valid());
  assert(application.network.is_valid());
  assert(application.aether.is_valid());
  apptraverse::BeginRuntimeSession(*application.runtime, *application.network,
                                   *application.aether);
  if (application.local_client.is_valid()) {
    static_cast<void>(CommitPresenceMonitoringStarted(*application.local_client));
  }
}

// FIRST: AetherRegistrationCompletedEvent
// SECOND: ClientAddedEvent on ChatRoom (if needed)
// THIRD: PresenceMonitoringStartedEvent -> Presence = Connecting
inline bool CompleteLocalRegistration(
    ChatApplication& application, std::string uid,
    apptraverse::ModelRuntime* runtime = nullptr) {
  if (uid.empty() || !application.aether.is_valid() ||
      !application.room.is_valid()) {
    return false;
  }
  bool const already_complete =
      application.aether->IsRegisteredForCurrentRun() &&
      application.local_client.is_valid() &&
      application.room->HasClient(application.local_client.id().id());
  if (already_complete) {
    return false;
  }
  static_cast<void>(
      apptraverse::CommitAetherRegistrationCompleted(*application.aether, uid));
  if (application.network.is_valid() && application.runtime.is_valid()) {
    static_cast<void>(apptraverse::CommitNetworkAvailable(
        *application.network, application.runtime->run_id));
  }
  auto client = CreateUnjoinedLocalClient(application, uid);
  if (runtime != nullptr) {
    runtime->AttachNode(*client, *application.room);
  }
  if (!application.room->HasClient(client.id().id())) {
    CommitClientAdded(*application.room, *client);
  }
  static_cast<void>(CommitPresenceMonitoringStarted(*client));
  return true;
}

}  // namespace chat

#endif  // CHAT_COMMANDS_H_
