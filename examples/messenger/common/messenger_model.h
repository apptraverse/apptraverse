#ifndef APPTRAVERSE_MESSENGER_MODEL_H_
#define APPTRAVERSE_MESSENGER_MODEL_H_

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "aether-miscpp/reflect/reflect.h"
#include "aether-objects/obj/obj.h"

#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/presenter.h"
#include "apptraverse/shared_event_id.h"
#include "apptraverse/shared_event_order.h"
#include "apptraverse/shared_node.h"

#include "messenger_ids.h"

namespace apptraverse {

namespace example::chat_demo {
class IAetherFrameEndpoint;
}
class SharedSyncRuntime;

class Application;
class Surfaces;
class Surface;
class SurfacePresenter;
class Dialog;
class Conversation;
class SurfaceBoundsChangedEvent;
class SurfacePresentationSizeChangedEvent;
class OwnUidChangedEvent;
class PeerUidChangedEvent;
class DraftChangedEvent;
class MessageAppendedEvent;
class ConversationBoundEvent;
class LocalEndpointBoundEvent;
class MessageSequenceReservedEvent;
class MessageAddedEvent;

// One local transcript line. outgoing == true means this instance authored it.
struct MessengerMessage {
  std::string text;
  bool outgoing{true};

  bool operator==(MessengerMessage const& other) const noexcept {
    return text == other.text && outgoing == other.outgoing;
  }

  bool operator!=(MessengerMessage const& other) const noexcept {
    return !(*this == other);
  }

  AE_REFLECT_MEMBERS(text, outgoing)
};

// Archived conversation for a peer that is not currently active. Switching the
// active peer must not mix histories.
struct PeerConversation {
  std::string peer_uid;
  std::string draft;
  std::vector<MessengerMessage> messages;

  AE_REFLECT_MEMBERS(peer_uid, draft, messages)
};

// Shared journal message (replicated via MessageAddedEvent).
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

inline void AssignInitialDesktopBounds(Surface& surface);

// Single-surface host topology. List holds exactly one Surface after distill.
class Surface : public NodeFor<Surface> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::messenger::Surface", Surface,
                           Node, 1)

 protected:
  Surface() = default;

 public:
  explicit Surface(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(number), AE_MMBR(desktop_x), AE_MMBR(desktop_y),
                    AE_MMBR(desktop_width), AE_MMBR(desktop_height),
                    AE_MMBR(presentation_width), AE_MMBR(presentation_height),
                    AE_MMBR(surfaces), AE_MMBR(presenter), AE_MMBR(dialog))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv&) {
    throw std::runtime_error(
        "Surface v0 (pre-dialog) is not supported; start with a fresh state "
        "dir");
  }

  template <typename Dnv>
  void Load(ae::Version<1>, Dnv& dnv) {
    dnv(base_, number, desktop_x, desktop_y, desktop_width, desktop_height,
        presentation_width, presentation_height, surfaces, presenter, dialog);
  }

  template <typename Dnv>
  void Save(ae::Version<1>, Dnv& dnv) const {
    dnv(base_, number, desktop_x, desktop_y, desktop_width, desktop_height,
        presentation_width, presentation_height, surfaces, presenter, dialog);
  }

  void SetDesktopBounds(std::int32_t x, std::int32_t y, std::int32_t width,
                        std::int32_t height);
  void SetPresentationSize(std::int32_t width, std::int32_t height);

  void Apply(SurfaceBoundsChangedEvent const& event);
  void Apply(SurfacePresentationSizeChangedEvent const& event);

  std::uint32_t number{0};
  std::int32_t desktop_x{0};
  std::int32_t desktop_y{0};
  std::int32_t desktop_width{0};
  std::int32_t desktop_height{0};
  std::int32_t presentation_width{0};
  std::int32_t presentation_height{0};
  ae::ObjPtr<Surfaces> surfaces;
  ae::ObjPtr<SurfacePresenter> presenter;
  ae::ObjPtr<Dialog> dialog;
};

// Replicated pair journal. Sharing topology lives on SharedNode.
class Conversation
    : public NodeFor<Conversation, SharedNode> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::messenger::Conversation",
                           Conversation, SharedNode, 0)

 protected:
  Conversation() = default;

 public:
  explicit Conversation(ae::ObjProp prop) : NodeFor{prop} {}

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

// Active one-peer dialog + archived peers. Mutations only via Events.
class Dialog : public NodeFor<Dialog> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::messenger::Dialog", Dialog,
                           Node, 1)

 protected:
  Dialog() = default;

 public:
  explicit Dialog(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(own_uid), AE_MMBR(peer_uid), AE_MMBR(draft),
                    AE_MMBR(messages), AE_MMBR(archived), AE_MMBR(conversation))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, own_uid, peer_uid, draft, messages, archived);
  }

  template <typename Dnv>
  void Load(ae::Version<1>, Dnv& dnv) {
    dnv(base_, own_uid, peer_uid, draft, messages, archived, conversation);
  }

  template <typename Dnv>
  void Save(ae::Version<1>, Dnv& dnv) const {
    dnv(base_, own_uid, peer_uid, draft, messages, archived, conversation);
  }

  // Model-thread entry points (create Event + Commit).
  void SetOwnUid(std::string uid);
  void SetPeerUid(std::string uid);
  void SetDraft(std::string text);
  void BindConversation(Conversation::ptr next);

  void Apply(OwnUidChangedEvent const& event);
  void Apply(PeerUidChangedEvent const& event);
  void Apply(DraftChangedEvent const& event);
  void Apply(MessageAppendedEvent const& event);
  void Apply(ConversationBoundEvent const& event);

  std::string own_uid;
  std::string peer_uid;
  std::string draft;
  std::vector<MessengerMessage> messages;
  std::vector<PeerConversation> archived;
  Conversation::ptr conversation;
};

class SurfacePresenter : public Presenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::messenger::SurfacePresenter",
                           SurfacePresenter, Presenter, 0)

 protected:
  SurfacePresenter() = default;

 public:
  explicit SurfacePresenter(ae::ObjProp prop) : Presenter{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(surface))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, surface);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, surface);
  }

  void PresentationSizeChanged(std::int32_t width, std::int32_t height);

  // GUI → model via ObjId proxy. No field assignment on the mirror.
  void PeerUidEntered(std::string uid);
  void DraftEdited(std::string text);
  void SendDraft(std::string text);

  Surface::ptr surface;
};

class Surfaces : public NodeFor<Surfaces> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::messenger::Surfaces", Surfaces,
                           Node, 0)

 protected:
  Surfaces() = default;

 public:
  explicit Surfaces(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(surfaces))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, surfaces);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, surfaces);
  }

  std::vector<Surface::ptr> surfaces;
};

class SurfaceBoundsChangedEvent
    : public EventFor<Surface, SurfaceBoundsChangedEvent> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::messenger::SurfaceBoundsChangedEvent",
      SurfaceBoundsChangedEvent, Event, 0)

 protected:
  SurfaceBoundsChangedEvent() = default;

 public:
  explicit SurfaceBoundsChangedEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(x), AE_MMBR(y), AE_MMBR(width), AE_MMBR(height))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, x, y, width, height);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, x, y, width, height);
  }

  std::int32_t x{0};
  std::int32_t y{0};
  std::int32_t width{0};
  std::int32_t height{0};
};

class SurfacePresentationSizeChangedEvent
    : public EventFor<Surface, SurfacePresentationSizeChangedEvent> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::messenger::SurfacePresentationSizeChangedEvent",
      SurfacePresentationSizeChangedEvent, Event, 0)

 protected:
  SurfacePresentationSizeChangedEvent() = default;

 public:
  explicit SurfacePresentationSizeChangedEvent(ae::ObjProp prop)
      : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(width), AE_MMBR(height))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, width, height);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, width, height);
  }

  std::int32_t width{0};
  std::int32_t height{0};
};

class OwnUidChangedEvent : public EventFor<Dialog, OwnUidChangedEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::messenger::OwnUidChangedEvent",
                           OwnUidChangedEvent, Event, 0)

 protected:
  OwnUidChangedEvent() = default;

 public:
  explicit OwnUidChangedEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(uid))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, uid);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, uid);
  }

  std::string uid;
};

class PeerUidChangedEvent : public EventFor<Dialog, PeerUidChangedEvent> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::messenger::PeerUidChangedEvent", PeerUidChangedEvent,
      Event, 0)

 protected:
  PeerUidChangedEvent() = default;

 public:
  explicit PeerUidChangedEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(uid))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, uid);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, uid);
  }

  std::string uid;
};

class DraftChangedEvent : public EventFor<Dialog, DraftChangedEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::messenger::DraftChangedEvent",
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

// Local-only mirror append (tests / archive restore path). Outgoing shared
// sends use MessageAddedEvent on Conversation.
class MessageAppendedEvent : public EventFor<Dialog, MessageAppendedEvent> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::messenger::MessageAppendedEvent",
      MessageAppendedEvent, Event, 0)

 protected:
  MessageAppendedEvent() = default;

 public:
  explicit MessageAppendedEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(text), AE_MMBR(outgoing))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, text, outgoing);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, text, outgoing);
  }

  std::string text;
  bool outgoing{true};
};

class ConversationBoundEvent
    : public EventFor<Dialog, ConversationBoundEvent> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::messenger::ConversationBoundEvent",
      ConversationBoundEvent, Event, 0)

 protected:
  ConversationBoundEvent() = default;

 public:
  explicit ConversationBoundEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(conversation))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, conversation);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, conversation);
  }

  Conversation::ptr conversation;
};

class LocalEndpointBoundEvent
    : public EventFor<Application, LocalEndpointBoundEvent> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::messenger::LocalEndpointBoundEvent",
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
    : public EventFor<Application, MessageSequenceReservedEvent> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::messenger::MessageSequenceReservedEvent",
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

class MessageAddedEvent
    : public EventFor<Conversation, MessageAddedEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::messenger::MessageAddedEvent",
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

  bool MatchesSharedMetadata(SharedEventId const& identity,
                             SharedEventOrder const& order) const override {
    return message.id == identity &&
           message.timestamp_us == order.timestamp_us;
  }

  MessageValue message;
};

// Root Node: topology + local endpoint sequence for shared message identity.
class Application : public NodeFor<Application> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::messenger::Application",
                           Application, Node, 1)

 protected:
  Application() = default;

 public:
  explicit Application(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(surfaces), AE_MMBR(local_endpoint_uid),
                    AE_MMBR(next_message_sequence))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv&) {
    throw std::runtime_error(
        "Application v0 (pre-sync) is not supported; start with a fresh state "
        "dir");
  }

  template <typename Dnv>
  void Load(ae::Version<1>, Dnv& dnv) {
    dnv(base_, surfaces, local_endpoint_uid, next_message_sequence);
  }

  template <typename Dnv>
  void Save(ae::Version<1>, Dnv& dnv) const {
    dnv(base_, surfaces, local_endpoint_uid, next_message_sequence);
  }

  Surfaces::ptr surfaces;
  std::string local_endpoint_uid;
  std::uint64_t next_message_sequence{1};

  // Runtime-only (not reflected). Wired by MessengerModelSession.
  example::chat_demo::IAetherFrameEndpoint* aether{nullptr};
  SharedSyncRuntime* sync_runtime{nullptr};
  bool aether_ready{false};
  // Peer signaled dial readiness (inbound DialRequest or DialAck) for sync.
  bool peer_prepared_for_sync{false};
  std::chrono::steady_clock::time_point last_dial_send{};

  void Apply(LocalEndpointBoundEvent const& event);
  bool CanApply(MessageSequenceReservedEvent const& event) const;
  void Apply(MessageSequenceReservedEvent const& event);

  // Model-thread: validate peer UID, SetPeerUid via Event, OpenPeer when ready.
  void ConfirmPeerUid(std::string raw);
  void OnAetherLocalUid(std::string uid);
  void OnAetherReady();
  // Control-plane dial only. source_uid is the Æther transport identity.
  void OnControlMessage(std::string source_uid,
                        std::vector<std::uint8_t> bytes);
  void AppendOutgoingMessage(std::string text);
  void SetupActivePeerSync();
  void TeardownPeer(std::string const& peer_uid);
  void DriveConversationSync();
  // True while peer is set and journal sync may still need dial / drive ticks.
  bool NeedsPeriodicSyncWake() const;

 private:
  void SendDialControl(bool ack);
  void MaybeRetryDial();
};

inline void AssignInitialDesktopBounds(Surface& surface) {
  constexpr std::int32_t kWidth = 640;
  constexpr std::int32_t kHeight = 480;
  constexpr std::int32_t kBaseX = 120;
  constexpr std::int32_t kBaseY = 120;
  surface.desktop_x = kBaseX;
  surface.desktop_y = kBaseY;
  surface.desktop_width = kWidth;
  surface.desktop_height = kHeight;
  surface.presentation_width = kWidth;
  surface.presentation_height = kHeight;
}

void EnsureMessengerModelRegistration();

}  // namespace apptraverse

#endif  // APPTRAVERSE_MESSENGER_MODEL_H_
