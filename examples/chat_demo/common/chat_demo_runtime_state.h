#ifndef APPTRAVERSE_EXAMPLE_CHAT_DEMO_CHAT_DEMO_RUNTIME_STATE_H_
#define APPTRAVERSE_EXAMPLE_CHAT_DEMO_CHAT_DEMO_RUNTIME_STATE_H_

#include <cstdint>
#include <string>

#include "aether-objects/obj/obj_id.h"
#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"

namespace apptraverse::example::chat_demo {

enum class ChatJoinPhase : std::uint8_t {
  kIdle = 0,
  kJoining = 1,
  kAccepted = 2,
  kJoined = 3,
  kFailed = 4,
};

class JoinHostRequestedEvent;
class JoinHostAcceptedEvent;
class JoinHostCompletedEvent;
class JoinHostFailedEvent;
class CopyHostUidRequestedEvent;
class CopyHostUidCompletedEvent;

class ChatDemoRuntimeState
    : public apptraverse::NodeFor<ChatDemoRuntimeState> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::chat_demo::ChatDemoRuntimeState",
      ChatDemoRuntimeState, Node, 0)

 protected:
  ChatDemoRuntimeState() = default;

 public:
  explicit ChatDemoRuntimeState(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(join_phase), AE_MMBR(join_attempt_id),
                    AE_MMBR(expected_host_uid), AE_MMBR(accepted_room_id),
                    AE_MMBR(bound_room_id), AE_MMBR(join_entry_id),
                    AE_MMBR(last_error), AE_MMBR(copy_request_id),
                    AE_MMBR(copy_uid), AE_MMBR(copy_pending))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, join_phase, join_attempt_id, expected_host_uid, accepted_room_id,
        bound_room_id, join_entry_id, last_error, copy_request_id, copy_uid,
        copy_pending);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, join_phase, join_attempt_id, expected_host_uid, accepted_room_id,
        bound_room_id, join_entry_id, last_error, copy_request_id, copy_uid,
        copy_pending);
  }

  ChatJoinPhase join_phase{ChatJoinPhase::kIdle};
  ae::ObjId join_attempt_id;
  std::string expected_host_uid;
  ae::ObjId accepted_room_id;
  ae::ObjId bound_room_id;
  ae::ObjId join_entry_id;
  std::string last_error;
  ae::ObjId copy_request_id;
  std::string copy_uid;
  bool copy_pending{false};

  bool CanApply(JoinHostRequestedEvent const& event) const;
  void Apply(JoinHostRequestedEvent const& event);
  bool CanApply(JoinHostAcceptedEvent const& event) const;
  void Apply(JoinHostAcceptedEvent const& event);
  bool CanApply(JoinHostCompletedEvent const& event) const;
  void Apply(JoinHostCompletedEvent const& event);
  bool CanApply(JoinHostFailedEvent const& event) const;
  void Apply(JoinHostFailedEvent const& event);
  void Apply(CopyHostUidRequestedEvent const& event);
  void Apply(CopyHostUidCompletedEvent const& event);
};

class JoinHostRequestedEvent
    : public apptraverse::EventFor<ChatDemoRuntimeState,
                                   JoinHostRequestedEvent> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::chat_demo::JoinHostRequestedEvent",
      JoinHostRequestedEvent, Event, 0)

 protected:
  JoinHostRequestedEvent() = default;

 public:
  explicit JoinHostRequestedEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(entry_id), AE_MMBR(host_uid))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, entry_id, host_uid);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, entry_id, host_uid);
  }

  ae::ObjId entry_id;
  std::string host_uid;
};

class JoinHostAcceptedEvent
    : public apptraverse::EventFor<ChatDemoRuntimeState, JoinHostAcceptedEvent> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::chat_demo::JoinHostAcceptedEvent",
      JoinHostAcceptedEvent, Event, 0)

 protected:
  JoinHostAcceptedEvent() = default;

 public:
  explicit JoinHostAcceptedEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(attempt_id), AE_MMBR(source_uid), AE_MMBR(room_id))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, attempt_id, source_uid, room_id);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, attempt_id, source_uid, room_id);
  }

  ae::ObjId attempt_id;
  std::string source_uid;
  ae::ObjId room_id;
};

class JoinHostCompletedEvent
    : public apptraverse::EventFor<ChatDemoRuntimeState,
                                   JoinHostCompletedEvent> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::chat_demo::JoinHostCompletedEvent",
      JoinHostCompletedEvent, Event, 0)

 protected:
  JoinHostCompletedEvent() = default;

 public:
  explicit JoinHostCompletedEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(attempt_id), AE_MMBR(room_id))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, attempt_id, room_id);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, attempt_id, room_id);
  }

  ae::ObjId attempt_id;
  ae::ObjId room_id;
};

class JoinHostFailedEvent
    : public apptraverse::EventFor<ChatDemoRuntimeState, JoinHostFailedEvent> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::chat_demo::JoinHostFailedEvent",
      JoinHostFailedEvent, Event, 0)

 protected:
  JoinHostFailedEvent() = default;

 public:
  explicit JoinHostFailedEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(attempt_id), AE_MMBR(reason))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, attempt_id, reason);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, attempt_id, reason);
  }

  ae::ObjId attempt_id;
  std::string reason;
};

class CopyHostUidRequestedEvent
    : public apptraverse::EventFor<ChatDemoRuntimeState,
                                   CopyHostUidRequestedEvent> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::chat_demo::CopyHostUidRequestedEvent",
      CopyHostUidRequestedEvent, Event, 0)

 protected:
  CopyHostUidRequestedEvent() = default;

 public:
  explicit CopyHostUidRequestedEvent(ae::ObjProp prop) : EventFor{prop} {}

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

class CopyHostUidCompletedEvent
    : public apptraverse::EventFor<ChatDemoRuntimeState,
                                   CopyHostUidCompletedEvent> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::chat_demo::CopyHostUidCompletedEvent",
      CopyHostUidCompletedEvent, Event, 0)

 protected:
  CopyHostUidCompletedEvent() = default;

 public:
  explicit CopyHostUidCompletedEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(request_id), AE_MMBR(ok), AE_MMBR(detail))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, request_id, ok, detail);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, request_id, ok, detail);
  }

  ae::ObjId request_id;
  bool ok{false};
  std::string detail;
};

void EnsureChatDemoRuntimeRegistration();

}  // namespace apptraverse::example::chat_demo

#endif  // APPTRAVERSE_EXAMPLE_CHAT_DEMO_CHAT_DEMO_RUNTIME_STATE_H_
