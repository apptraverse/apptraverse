#ifndef APPTRAVERSE_CHAT_EVENTS_H_
#define APPTRAVERSE_CHAT_EVENTS_H_

#include "apptraverse/event_for.h"
#include "apptraverse/object_macros.h"
#include "chat_model.h"

namespace apptraverse {

class JoinEvent : public EventFor<ChatRoom, JoinEvent> {
  APPTRAVERSE_OBJECT(JoinEvent, Event, 0)

 protected:
  JoinEvent() = default;

 public:
  explicit JoinEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(client))

  ChatClient::ptr client;
};

class ChatMessageEvent : public EventFor<ChatRoom, ChatMessageEvent> {
  APPTRAVERSE_OBJECT(ChatMessageEvent, Event, 0)

 protected:
  ChatMessageEvent() = default;

 public:
  explicit ChatMessageEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(author), AE_MMBR(text))

  ChatClient::ptr author;
  ImmutableString::ptr text;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_CHAT_EVENTS_H_
