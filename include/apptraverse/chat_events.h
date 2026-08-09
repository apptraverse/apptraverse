#ifndef APPTRAVERSE_CHAT_EVENTS_H_
#define APPTRAVERSE_CHAT_EVENTS_H_

#include <string>

#include "apptraverse/client.h"
#include "apptraverse/event_for.h"

namespace apptraverse {

class Chat;

class JoinClientEvent : public EventFor<Chat, JoinClientEvent> {
  AE_OBJECT(JoinClientEvent, Event, 0)

 protected:
  JoinClientEvent() = default;

 public:
  explicit JoinClientEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(client))

  Client::ptr client;
};

class AddMessageEvent : public EventFor<Chat, AddMessageEvent> {
  AE_OBJECT(AddMessageEvent, Event, 0)

 protected:
  AddMessageEvent() = default;

 public:
  explicit AddMessageEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(author), AE_MMBR(text))

  Client::ptr author;
  std::string text;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_CHAT_EVENTS_H_
