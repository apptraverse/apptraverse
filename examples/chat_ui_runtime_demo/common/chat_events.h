#ifndef CHAT_EVENTS_H_
#define CHAT_EVENTS_H_

#include <cstdint>
#include <string>

#include "aether-objects/obj/obj.h"

#include "apptraverse/event_for.h"
#include "apptraverse/object_macros.h"
#include "chat_model.h"

namespace chat {

using apptraverse::Event;
using apptraverse::EventFor;

class ClientAddedEvent : public EventFor<ChatRoom, ClientAddedEvent> {
  APPTRAVERSE_NAMED_OBJECT("chat::ClientAddedEvent", ClientAddedEvent, Event, 0)

 protected:
  ClientAddedEvent() = default;

 public:
  explicit ClientAddedEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(client))

  ChatClient::ptr client;
};

class ChatMessageEvent : public EventFor<ChatRoom, ChatMessageEvent> {
  APPTRAVERSE_NAMED_OBJECT("chat::ChatMessageEvent", ChatMessageEvent, Event, 1)

 protected:
  ChatMessageEvent() = default;

 public:
  explicit ChatMessageEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(author), AE_MMBR(text), AE_MMBR(sent_at_unix_ms))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, author, text);
    sent_at_unix_ms = 0;
  }

  template <typename Dnv>
  void Load(ae::Version<1>, Dnv& dnv) {
    dnv(base_, author, text, sent_at_unix_ms);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, author, text);
  }

  template <typename Dnv>
  void Save(ae::Version<1>, Dnv& dnv) const {
    dnv(base_, author, text, sent_at_unix_ms);
  }

  ChatClient::ptr author;
  ImmutableString::ptr text;
  std::int64_t sent_at_unix_ms{0};
};

class PresenceChangedEvent : public EventFor<ChatClient, PresenceChangedEvent> {
  APPTRAVERSE_NAMED_OBJECT("chat::PresenceChangedEvent", PresenceChangedEvent,
                           Event, 0)

 protected:
  PresenceChangedEvent() = default;

 public:
  explicit PresenceChangedEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(presence))

  std::uint8_t presence{static_cast<std::uint8_t>(PresenceState::kUnknown)};

  PresenceState GetPresence() const {
    return static_cast<PresenceState>(presence);
  }

  void SetPresence(PresenceState state) {
    presence = static_cast<std::uint8_t>(state);
  }
};

class PresenceMonitoringStartedEvent
    : public EventFor<ChatClient, PresenceMonitoringStartedEvent> {
  APPTRAVERSE_NAMED_OBJECT("chat::PresenceMonitoringStartedEvent",
                           PresenceMonitoringStartedEvent, Event, 0)

 protected:
  PresenceMonitoringStartedEvent() = default;

 public:
  explicit PresenceMonitoringStartedEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT()
};

}  // namespace chat

#endif  // CHAT_EVENTS_H_
