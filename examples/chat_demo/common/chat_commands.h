#ifndef APPTRAVERSE_EXAMPLE_CHAT_DEMO_CHAT_COMMANDS_H_
#define APPTRAVERSE_EXAMPLE_CHAT_DEMO_CHAT_COMMANDS_H_

#include <cstdint>
#include <functional>
#include <string>

#include "aether-objects/obj/obj_id.h"
#include "apptraverse/event_for.h"
#include "apptraverse/link.h"
#include "apptraverse/shared_event_id.h"
#include "apptraverse/shared_event_order.h"

#include "chat_model.h"

namespace apptraverse::example::chat_demo {

// Persistence callback provided by the host/caller.
using PersistLocalState = std::function<void()>;

// Local events for ChatWorkspace
class ChatEntryAddedEvent
    : public apptraverse::EventFor<ChatWorkspace, ChatEntryAddedEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::chat_demo::ChatEntryAddedEvent",
                           ChatEntryAddedEvent, Event, 0)

 protected:
  ChatEntryAddedEvent() = default;

 public:
  explicit ChatEntryAddedEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(entry))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, entry);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, entry);
  }

  ChatEntry::ptr entry;
};

class ChatSelectedEvent
    : public apptraverse::EventFor<ChatWorkspace, ChatSelectedEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::chat_demo::ChatSelectedEvent",
                           ChatSelectedEvent, Event, 0)

 protected:
  ChatSelectedEvent() = default;

 public:
  explicit ChatSelectedEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(entry_id))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, entry_id);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, entry_id);
  }

  ae::ObjId entry_id;
};

class LocalEndpointBoundEvent
    : public apptraverse::EventFor<ChatWorkspace, LocalEndpointBoundEvent> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::chat_demo::LocalEndpointBoundEvent",
      LocalEndpointBoundEvent, Event, 0)

 protected:
  LocalEndpointBoundEvent() = default;

 public:
  explicit LocalEndpointBoundEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(endpoint_uid))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, endpoint_uid);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, endpoint_uid);
  }

  std::string endpoint_uid;
};

class MessageSequenceReservedEvent
    : public apptraverse::EventFor<ChatWorkspace, MessageSequenceReservedEvent> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::chat_demo::MessageSequenceReservedEvent",
      MessageSequenceReservedEvent, Event, 0)

 protected:
  MessageSequenceReservedEvent() = default;

 public:
  explicit MessageSequenceReservedEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(reserved_sequence))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, reserved_sequence);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, reserved_sequence);
  }

  std::uint64_t reserved_sequence{0};
};

class DesktopBoundsChangedEvent
    : public apptraverse::EventFor<ChatWorkspace, DesktopBoundsChangedEvent> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::chat_demo::DesktopBoundsChangedEvent",
      DesktopBoundsChangedEvent, Event, 0)

 protected:
  DesktopBoundsChangedEvent() = default;

 public:
  explicit DesktopBoundsChangedEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(bounds))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, bounds);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, bounds);
  }

  DesktopBounds bounds;
};

class DemoRoleConfiguredEvent
    : public apptraverse::EventFor<ChatWorkspace, DemoRoleConfiguredEvent> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::chat_demo::DemoRoleConfiguredEvent",
      DemoRoleConfiguredEvent, Event, 0)

 protected:
  DemoRoleConfiguredEvent() = default;

 public:
  explicit DemoRoleConfiguredEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(role))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, role);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, role);
  }

  DemoRole role{DemoRole::kUnconfigured};
};

class HostUidInputChangedEvent
    : public apptraverse::EventFor<ChatWorkspace, HostUidInputChangedEvent> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::chat_demo::HostUidInputChangedEvent",
      HostUidInputChangedEvent, Event, 0)

 protected:
  HostUidInputChangedEvent() = default;

 public:
  explicit HostUidInputChangedEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(text))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, text);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, text);
  }

  std::string text;
};

// Local events for ChatEntry
class ChatBindingChangedEvent
    : public apptraverse::EventFor<ChatEntry, ChatBindingChangedEvent> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::chat_demo::ChatBindingChangedEvent",
      ChatBindingChangedEvent, Event, 0)

 protected:
  ChatBindingChangedEvent() = default;

 public:
  explicit ChatBindingChangedEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(peer_link), AE_MMBR(room))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, peer_link, room);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, peer_link, room);
  }

  apptraverse::Link::ptr peer_link;
  ChatRoom::ptr room;
};

class DraftChangedEvent
    : public apptraverse::EventFor<ChatEntry, DraftChangedEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::chat_demo::DraftChangedEvent",
                           DraftChangedEvent, Event, 0)

 protected:
  DraftChangedEvent() = default;

 public:
  explicit DraftChangedEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(text))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, text);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, text);
  }

  std::string text;
};

class ScrollChangedEvent
    : public apptraverse::EventFor<ChatEntry, ScrollChangedEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::chat_demo::ScrollChangedEvent",
                           ScrollChangedEvent, Event, 0)

 protected:
  ScrollChangedEvent() = default;

 public:
  explicit ScrollChangedEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(scroll))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, scroll);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, scroll);
  }

  ScrollAnchor scroll;
};

// Event for ChatRoom (Shared replication)
class MessageAddedEvent
    : public apptraverse::EventFor<ChatRoom, MessageAddedEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::chat_demo::MessageAddedEvent",
                           MessageAddedEvent, Event, 0)

 protected:
  MessageAddedEvent() = default;

 public:
  explicit MessageAddedEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(message))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, message);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, message);
  }

  bool MatchesSharedMetadata(
      apptraverse::SharedEventId const& identity,
      apptraverse::SharedEventOrder const& order) const override {
    return message.id == identity &&
           message.timestamp_us == order.timestamp_us;
  }

  MessageValue message;
};

// Common command API
bool ConfigureDemoRole(ChatWorkspace& workspace, DemoRole role,
                       PersistLocalState const& persist = {});

bool SetHostUidInput(ChatWorkspace& workspace, std::string const& text,
                     PersistLocalState const& persist = {});

std::string ConversationDisplayName(DemoRole local_role,
                                    std::string const& peer_uid);

ChatEntry::ptr OpenOrSelectChat(ChatWorkspace& workspace,
                                std::string const& peer_uid,
                                PersistLocalState const& persist = {});

bool BindChat(ChatEntry& entry, apptraverse::Link::ptr link,
              ChatRoom::ptr room, PersistLocalState const& persist = {});

bool BindLocalEndpoint(ChatWorkspace& workspace, std::string const& uid,
                       PersistLocalState const& persist = {});

bool SetDraft(ChatEntry& entry, std::string const& text,
              PersistLocalState const& persist = {});

bool SetScroll(ChatEntry& entry, ScrollAnchor const& anchor,
               PersistLocalState const& persist = {});

bool SetDesktopBounds(ChatWorkspace& workspace, DesktopBounds const& bounds,
                      PersistLocalState const& persist = {});

bool SelectChat(ChatWorkspace& workspace, ae::ObjId entry_id,
                PersistLocalState const& persist = {});

SharedEventId SubmitDraft(ChatWorkspace& workspace, ChatEntry& entry,
                          std::uint64_t now_us,
                          PersistLocalState const& persist = {});

}  // namespace apptraverse::example::chat_demo

#endif  // APPTRAVERSE_EXAMPLE_CHAT_DEMO_CHAT_COMMANDS_H_
