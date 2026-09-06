#ifndef CHAT_MODEL_H_
#define CHAT_MODEL_H_

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <cassert>

#include "aether-objects/obj/obj.h"

#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/runtime_lifecycle.h"

#include "chat_ids.h"
#include "chat_presence.h"

namespace chat {

using apptraverse::AetherRegistrationState;
using apptraverse::ApplicationRuntimeState;
using apptraverse::NetworkState;
using apptraverse::Node;
using apptraverse::NodeFor;

enum class ChatRole : std::uint32_t {
  Host = 0,
  Client = 1,
};

class ClientAddedEvent;
class ChatMessageEvent;
class PresenceChangedEvent;
class PresenceMonitoringStartedEvent;

class ImmutableString : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("chat::ImmutableString", ImmutableString, ae::Obj, 0)

 protected:
  ImmutableString() = default;

 public:
  explicit ImmutableString(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(bytes))

  std::string bytes;
};

class ChatClient;
class ChatFeedItem;
class ChatRoom;
class ChatApplication;

inline constexpr std::uint32_t kChatFeedKindJoin = 1;
inline constexpr std::uint32_t kChatFeedKindMessage = 2;

class ChatClient : public NodeFor<ChatClient> {
  APPTRAVERSE_NAMED_OBJECT("chat::ChatClient", ChatClient, Node, 1)

 protected:
  ChatClient() = default;

 public:
  explicit ChatClient(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(display_name), AE_MMBR(aether_uid),
                    AE_MMBR(presence))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv&) {
    throw std::runtime_error(
        "ChatClient v0 is not supported; start with a fresh state dir");
  }

  template <typename Dnv>
  void Load(ae::Version<1>, Dnv& dnv) {
    Node::Load(ae::Version<1>{}, dnv);
    dnv(display_name, aether_uid, presence);
  }

  template <typename Dnv>
  void Save(ae::Version<1>, Dnv& dnv) const {
    Node::Save(ae::Version<1>{}, dnv);
    dnv(display_name, aether_uid, presence);
  }

  ImmutableString::ptr display_name;
  ImmutableString::ptr aether_uid;
  std::uint8_t presence{static_cast<std::uint8_t>(PresenceState::kUnknown)};

  PresenceState GetPresence() const {
    return static_cast<PresenceState>(presence);
  }

  std::string DisplayNameBytes() const {
    if (!display_name.is_valid()) {
      return {};
    }
    display_name.Load();
    assert(display_name.is_loaded());
    return display_name->bytes;
  }

  std::string AetherUidText() const {
    if (!aether_uid.is_valid()) {
      return {};
    }
    aether_uid.Load();
    assert(aether_uid.is_loaded());
    return aether_uid->bytes;
  }

  void SetAetherUidText(std::string uid) {
    auto next = ImmutableString::ptr::Create(ae::CreateWith{*domain});
    next->bytes = std::move(uid);
    aether_uid = next;
    NoteMaterializedChange();
  }

  // Not the authoritative Presence path. Use CommitPresenceChanged /
  // CommitPresenceMonitoringStarted.
  void SetPresence(PresenceState value) {
    auto const raw = static_cast<std::uint8_t>(value);
    if (presence == raw) {
      return;
    }
    presence = raw;
    NoteMaterializedChange();
  }

  bool CanApply(PresenceChangedEvent const& event) const;
  bool CanApply(PresenceMonitoringStartedEvent const& event) const;
  void Apply(PresenceChangedEvent const& event);
  void Apply(PresenceMonitoringStartedEvent const& event);
};

class ChatFeedItem : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("chat::ChatFeedItem", ChatFeedItem, ae::Obj, 1)

 protected:
  ChatFeedItem() = default;

 public:
  explicit ChatFeedItem(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(kind), AE_MMBR(client), AE_MMBR(body),
                    AE_MMBR(sent_at_unix_ms), AE_MMBR(source_event_obj_id))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, kind, client, body);
    sent_at_unix_ms = 0;
    source_event_obj_id = 0;
  }

  template <typename Dnv>
  void Load(ae::Version<1>, Dnv& dnv) {
    dnv(base_, kind, client, body, sent_at_unix_ms, source_event_obj_id);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, kind, client, body);
  }

  template <typename Dnv>
  void Save(ae::Version<1>, Dnv& dnv) const {
    dnv(base_, kind, client, body, sent_at_unix_ms, source_event_obj_id);
  }

  std::uint32_t kind{0};
  ChatClient::ptr client;
  ImmutableString::ptr body;
  std::int64_t sent_at_unix_ms{0};
  std::uint32_t source_event_obj_id{0};
};

class ChatRoom : public NodeFor<ChatRoom> {
  APPTRAVERSE_NAMED_OBJECT("chat::ChatRoom", ChatRoom, Node, 1)

 protected:
  ChatRoom() = default;

 public:
  explicit ChatRoom(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(clients), AE_MMBR(feed))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv&) {
    throw std::runtime_error(
        "ChatRoom v0 is not supported; start with a fresh state dir");
  }

  template <typename Dnv>
  void Load(ae::Version<1>, Dnv& dnv) {
    Node::Load(ae::Version<1>{}, dnv);
    dnv(clients, feed);
  }

  template <typename Dnv>
  void Save(ae::Version<1>, Dnv& dnv) const {
    Node::Save(ae::Version<1>{}, dnv);
    dnv(clients, feed);
  }

  std::vector<ChatClient::ptr> clients;
  std::vector<ChatFeedItem::ptr> feed;

  bool CanApply(ClientAddedEvent const& event) const;
  bool CanApply(ChatMessageEvent const& event) const;
  void Apply(ClientAddedEvent const& event);
  void Apply(ChatMessageEvent const& event);

  bool HasClient(std::uint32_t client_id) const;
  bool HasClientByAetherUid(std::string const& uid) const;
  ChatClient::ptr FindClientByAetherUid(std::string const& uid) const;
};

class ChatApplication : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("chat::ChatApplication", ChatApplication, ae::Obj, 0)

 protected:
  ChatApplication() = default;

 public:
  explicit ChatApplication(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(room), AE_MMBR(local_client), AE_MMBR(runtime),
                    AE_MMBR(network), AE_MMBR(aether),
                    AE_MMBR(local_display_name), AE_MMBR(role))

  ChatRoom::ptr room;
  ChatClient::ptr local_client;
  ApplicationRuntimeState::ptr runtime;
  NetworkState::ptr network;
  AetherRegistrationState::ptr aether;
  ImmutableString::ptr local_display_name;
  std::uint32_t role{static_cast<std::uint32_t>(ChatRole::Host)};

  ChatRole GetRole() const { return static_cast<ChatRole>(role); }

  void SetRole(ChatRole value) { role = static_cast<std::uint32_t>(value); }

  std::string LocalDisplayNameBytes() const {
    if (!local_display_name.is_valid()) {
      return {};
    }
    local_display_name.Load();
    assert(local_display_name.is_loaded());
    return local_display_name->bytes;
  }
};

void EnsureChatRegistration();

}  // namespace chat

#endif  // CHAT_MODEL_H_
