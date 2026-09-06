#ifndef APPTRAVERSE_CHAT_EVENTS_H_
#define APPTRAVERSE_CHAT_EVENTS_H_

#include <cstdint>

#include "aether-objects/obj/obj.h"

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
  APPTRAVERSE_OBJECT(ChatMessageEvent, Event, 1)

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

// Runtime-only local connectivity Presence. Applied to ChatClient; not part of
// the shared ChatRoom journal / P2P replication.
class LocalPresenceEvent : public EventFor<ChatClient, LocalPresenceEvent> {
  APPTRAVERSE_OBJECT(LocalPresenceEvent, Event, 0)

 protected:
  LocalPresenceEvent() = default;

 public:
  explicit LocalPresenceEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(presence))

  std::uint8_t presence{static_cast<std::uint8_t>(PresenceState::kUnknown)};

  PresenceState GetPresence() const {
    return static_cast<PresenceState>(presence);
  }

  void SetPresence(PresenceState state) {
    presence = static_cast<std::uint8_t>(state);
  }
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_CHAT_EVENTS_H_
