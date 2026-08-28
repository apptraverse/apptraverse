#ifndef APPTRAVERSE_CHAT_MODEL_H_
#define APPTRAVERSE_CHAT_MODEL_H_

#include <cstdint>
#include <string>
#include <vector>

#include <cassert>

#include "aether/obj/obj.h"

#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"

#include "chat_ids.h"

namespace apptraverse {

enum class ChatRole : std::uint32_t {
  Host = 0,
  Client = 1,
};

class JoinEvent;
class ChatMessageEvent;

class ImmutableString : public ae::Obj {
  APPTRAVERSE_OBJECT(ImmutableString, ae::Obj, 0)

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

inline constexpr std::uint32_t kChatFeedKindJoin = 1;
inline constexpr std::uint32_t kChatFeedKindMessage = 2;

class ChatClient : public NodeFor<ChatClient> {
  APPTRAVERSE_OBJECT(ChatClient, Node, 0)

 protected:
  ChatClient() = default;

 public:
  explicit ChatClient(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(display_name), AE_MMBR(aether_uid), AE_MMBR(online))

  ImmutableString::ptr display_name;
  ImmutableString::ptr aether_uid;
  bool online{false};

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

  void SetOnline(bool value) {
    if (online == value) {
      return;
    }
    online = value;
    NoteMaterializedChange();
  }
};

class ChatFeedItem : public ae::Obj {
  APPTRAVERSE_OBJECT(ChatFeedItem, ae::Obj, 0)

 protected:
  ChatFeedItem() = default;

 public:
  explicit ChatFeedItem(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(kind), AE_MMBR(client), AE_MMBR(body))

  std::uint32_t kind{0};
  ChatClient::ptr client;
  ImmutableString::ptr body;
};

class ChatRoom : public NodeFor<ChatRoom> {
  APPTRAVERSE_OBJECT(ChatRoom, Node, 0)

 protected:
  ChatRoom() = default;

 public:
  explicit ChatRoom(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(clients), AE_MMBR(feed))

  std::vector<ChatClient::ptr> clients;
  std::vector<ChatFeedItem::ptr> feed;

  bool CanApply(JoinEvent const& event) const;
  bool CanApply(ChatMessageEvent const& event) const;
  void Apply(JoinEvent const& event);
  void Apply(ChatMessageEvent const& event);

  bool HasClient(std::uint32_t client_id) const;
  bool HasClientByAetherUid(std::string const& uid) const;
  ChatClient::ptr FindClientByAetherUid(std::string const& uid) const;
};

class LocalAetherIdentity : public NodeFor<LocalAetherIdentity> {
  APPTRAVERSE_OBJECT(LocalAetherIdentity, Node, 0)

 protected:
  LocalAetherIdentity() = default;

 public:
  explicit LocalAetherIdentity(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(uid_text))

  ImmutableString::ptr uid_text;

  std::string UidTextBytes() const {
    if (!uid_text.is_valid()) {
      return {};
    }
    uid_text.Load();
    assert(uid_text.is_loaded());
    return uid_text->bytes;
  }

  void SetUidTextBytes(std::string bytes) {
    auto next = ImmutableString::ptr::Create(ae::CreateWith{*domain});
    next->bytes = std::move(bytes);
    uid_text = next;
    NoteMaterializedChange();
  }
};

class Application : public ae::Obj {
  APPTRAVERSE_OBJECT(Application, ae::Obj, 0)

 protected:
  Application() = default;

 public:
  explicit Application(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(chat_room), AE_MMBR(host_client),
                    AE_MMBR(local_aether), AE_MMBR(role))

  ChatRoom::ptr chat_room;
  ChatClient::ptr host_client;
  LocalAetherIdentity::ptr local_aether;
  std::uint32_t role{static_cast<std::uint32_t>(ChatRole::Host)};

  ChatRole GetRole() const { return static_cast<ChatRole>(role); }

  void SetRole(ChatRole value) { role = static_cast<std::uint32_t>(value); }
};

void EnsureChatRegistration();

}  // namespace apptraverse

#endif  // APPTRAVERSE_CHAT_MODEL_H_
