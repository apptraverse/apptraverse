#include "chat_demo_runtime_state.h"

namespace apptraverse::example::chat_demo {
namespace {

APPTRAVERSE_REGISTER(ChatDemoRuntimeState);
APPTRAVERSE_REGISTER(JoinHostRequestedEvent);
APPTRAVERSE_REGISTER(JoinHostAcceptedEvent);
APPTRAVERSE_REGISTER(JoinHostCompletedEvent);
APPTRAVERSE_REGISTER(JoinHostFailedEvent);
APPTRAVERSE_REGISTER(CopyHostUidRequestedEvent);
APPTRAVERSE_REGISTER(CopyHostUidCompletedEvent);

}  // namespace

bool ChatDemoRuntimeState::CanApply(JoinHostRequestedEvent const& event) const {
  return event.entry_id.is_valid() && !event.host_uid.empty();
}

void ChatDemoRuntimeState::Apply(JoinHostRequestedEvent const& event) {
  join_phase = ChatJoinPhase::kJoining;
  join_attempt_id = event.obj_id;
  expected_host_uid = event.host_uid;
  join_entry_id = event.entry_id;
  accepted_room_id = {};
  bound_room_id = {};
  last_error.clear();
  NoteMaterializedChange();
}

bool ChatDemoRuntimeState::CanApply(JoinHostAcceptedEvent const& event) const {
  return join_phase == ChatJoinPhase::kJoining &&
         event.attempt_id == join_attempt_id && event.room_id.is_valid() &&
         event.source_uid == expected_host_uid;
}

void ChatDemoRuntimeState::Apply(JoinHostAcceptedEvent const& event) {
  join_phase = ChatJoinPhase::kAccepted;
  accepted_room_id = event.room_id;
  last_error.clear();
  NoteMaterializedChange();
}

bool ChatDemoRuntimeState::CanApply(JoinHostCompletedEvent const& event) const {
  if (!event.attempt_id.is_valid() || !event.room_id.is_valid()) {
    return false;
  }
  // Idempotent re-complete for the same attempt/room while already Joined.
  if (join_phase == ChatJoinPhase::kJoined &&
      accepted_room_id == event.room_id &&
      join_attempt_id == event.attempt_id) {
    return true;
  }
  // Room bind requires a matching Accepted identity first (not bare Joining).
  if (join_phase != ChatJoinPhase::kAccepted) {
    return false;
  }
  if (event.attempt_id != join_attempt_id) {
    return false;
  }
  if (!accepted_room_id.is_valid() || event.room_id != accepted_room_id) {
    return false;
  }
  return true;
}

void ChatDemoRuntimeState::Apply(JoinHostCompletedEvent const& event) {
  join_phase = ChatJoinPhase::kJoined;
  bound_room_id = event.room_id;
  accepted_room_id = event.room_id;
  last_error.clear();
  NoteMaterializedChange();
}

bool ChatDemoRuntimeState::CanApply(JoinHostFailedEvent const& event) const {
  if (event.reason.empty()) {
    return false;
  }
  if (join_phase == ChatJoinPhase::kJoining ||
      join_phase == ChatJoinPhase::kAccepted) {
    if (!event.attempt_id.is_valid()) {
      return false;
    }
    if (join_attempt_id.is_valid() && event.attempt_id != join_attempt_id) {
      return false;
    }
    return true;
  }
  // Pre-attempt validation failure (e.g. invalid Host UID) while Idle/Failed.
  if (join_phase == ChatJoinPhase::kIdle ||
      join_phase == ChatJoinPhase::kFailed) {
    return true;
  }
  return false;
}

void ChatDemoRuntimeState::Apply(JoinHostFailedEvent const& event) {
  join_phase = ChatJoinPhase::kFailed;
  if (event.attempt_id.is_valid()) {
    join_attempt_id = event.attempt_id;
  }
  last_error = event.reason;
  NoteMaterializedChange();
}

void ChatDemoRuntimeState::Apply(CopyHostUidRequestedEvent const& event) {
  copy_request_id = event.obj_id;
  copy_uid = event.uid;
  copy_pending = true;
  NoteMaterializedChange();
}

void ChatDemoRuntimeState::Apply(CopyHostUidCompletedEvent const& event) {
  if (event.request_id == copy_request_id) {
    copy_pending = false;
  }
  NoteMaterializedChange();
}

void EnsureChatDemoRuntimeRegistration() {
  (void)ChatDemoRuntimeState::kClassId;
}

}  // namespace apptraverse::example::chat_demo
