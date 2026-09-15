#ifndef APPTRAVERSE_EXAMPLE_CHAT_DEMO_CHAT_MODEL_H_
#define APPTRAVERSE_EXAMPLE_CHAT_DEMO_CHAT_MODEL_H_

#include <cstdint>
#include <string>
#include <vector>

#include "aether-miscpp/reflect/reflect.h"
#include "aether-objects/obj/obj.h"
#include "aether-objects/obj/obj_id.h"

#include "apptraverse/link.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/shared_event_id.h"
#include "apptraverse/shared_node.h"

namespace apptraverse::example::chat_demo {

// Reflected value type representing an immutable message in a chat room.
// The author is id.origin_uid.
struct MessageValue {
  SharedEventId id;
  std::uint64_t timestamp_us{0};
  std::string text;

  bool operator==(MessageValue const& other) const noexcept {
    return id == other.id && timestamp_us == other.timestamp_us &&
           text == other.text;
  }

  bool operator!=(MessageValue const& other) const noexcept {
    return !(*this == other);
  }

  AE_REFLECT_MEMBERS(id, timestamp_us, text)
};

class MessageAddedEvent;

// Replicated chat room derived from SharedNode.
// Owns the shared message timeline; sharing topology is owned by SharedNode.
class ChatRoom : public apptraverse::NodeFor<ChatRoom, apptraverse::SharedNode> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::chat_demo::ChatRoom",
                           ChatRoom, SharedNode, 0)

 protected:
  ChatRoom() = default;

 public:
  explicit ChatRoom(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(messages))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, messages);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, messages);
  }

  std::vector<MessageValue> messages;

  bool CanApply(MessageAddedEvent const& event) const;
  void Apply(MessageAddedEvent const& event);
};

// Reflected value representing view scroll position anchor.
struct ScrollAnchor {
  bool follow_tail{true};
  SharedEventId first_visible_message{};
  double offset_from_message_top{0.0};

  bool operator==(ScrollAnchor const& other) const noexcept {
    return follow_tail == other.follow_tail &&
           first_visible_message == other.first_visible_message &&
           offset_from_message_top == other.offset_from_message_top;
  }

  bool operator!=(ScrollAnchor const& other) const noexcept {
    return !(*this == other);
  }

  AE_REFLECT_MEMBERS(follow_tail, first_visible_message,
                     offset_from_message_top)
};

class ChatBindingChangedEvent;
class DraftChangedEvent;
class ScrollChangedEvent;

// Local workspace state for one chat peer/relationship.
// Not a SharedNode; local to this device.
class ChatEntry : public apptraverse::NodeFor<ChatEntry, apptraverse::Node> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::chat_demo::ChatEntry",
                           ChatEntry, Node, 0)

 protected:
  ChatEntry() = default;

 public:
  explicit ChatEntry(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(peer_admin_id), AE_MMBR(display_name),
                    AE_MMBR(peer_link), AE_MMBR(room), AE_MMBR(draft),
                    AE_MMBR(scroll))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, peer_admin_id, display_name, peer_link, room, draft, scroll);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, peer_admin_id, display_name, peer_link, room, draft, scroll);
  }

  std::string peer_admin_id;
  std::string display_name;
  apptraverse::Link::ptr peer_link;
  ChatRoom::ptr room;
  std::string draft;
  ScrollAnchor scroll;

  void Apply(ChatBindingChangedEvent const& event);
  void Apply(DraftChangedEvent const& event);
  void Apply(ScrollChangedEvent const& event);
};

// Reflected value for window bounds and maximized state on desktop hosts.
struct DesktopBounds {
  bool valid{false};
  std::int32_t x{0};
  std::int32_t y{0};
  std::int32_t width{1000};
  std::int32_t height{700};
  bool maximized{false};

  bool operator==(DesktopBounds const& other) const noexcept {
    return valid == other.valid && x == other.x && y == other.y &&
           width == other.width && height == other.height &&
           maximized == other.maximized;
  }

  bool operator!=(DesktopBounds const& other) const noexcept {
    return !(*this == other);
  }

  AE_REFLECT_MEMBERS(valid, x, y, width, height, maximized)
};

class ChatEntryAddedEvent;
class ChatSelectedEvent;
class LocalEndpointBoundEvent;
class MessageSequenceReservedEvent;
class DesktopBoundsChangedEvent;

// Locally persisted root for the chat application workspace.
// The workspace itself is never shared over the network.
class ChatWorkspace
    : public apptraverse::NodeFor<ChatWorkspace, apptraverse::Node> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::chat_demo::ChatWorkspace",
                           ChatWorkspace, Node, 0)

 protected:
  ChatWorkspace() = default;

 public:
  explicit ChatWorkspace(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(local_endpoint_uid),
                    AE_MMBR(next_message_sequence), AE_MMBR(chats),
                    AE_MMBR(selected_chat_id), AE_MMBR(desktop_bounds))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, local_endpoint_uid, next_message_sequence, chats,
        selected_chat_id, desktop_bounds);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, local_endpoint_uid, next_message_sequence, chats,
        selected_chat_id, desktop_bounds);
  }

  std::string local_endpoint_uid;
  std::uint64_t next_message_sequence{1};
  std::vector<ChatEntry::ptr> chats;
  ae::ObjId selected_chat_id;
  DesktopBounds desktop_bounds;

  void Apply(ChatEntryAddedEvent const& event);
  void Apply(ChatSelectedEvent const& event);
  void Apply(LocalEndpointBoundEvent const& event);
  void Apply(MessageSequenceReservedEvent const& event);
  void Apply(DesktopBoundsChangedEvent const& event);
};

void EnsureChatDemoModelRegistration();

}  // namespace apptraverse::example::chat_demo

#endif  // APPTRAVERSE_EXAMPLE_CHAT_DEMO_CHAT_MODEL_H_
