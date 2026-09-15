#include "chat_model.h"

#include <algorithm>

#include "apptraverse/object_macros.h"
#include "chat_commands.h"

namespace apptraverse::example::chat_demo {
namespace {

APPTRAVERSE_REGISTER(ChatRoom);
APPTRAVERSE_REGISTER(ChatEntry);
APPTRAVERSE_REGISTER(ChatWorkspace);
APPTRAVERSE_REGISTER(ChatEntryAddedEvent);
APPTRAVERSE_REGISTER(ChatSelectedEvent);
APPTRAVERSE_REGISTER(LocalEndpointBoundEvent);
APPTRAVERSE_REGISTER(MessageSequenceReservedEvent);
APPTRAVERSE_REGISTER(DesktopBoundsChangedEvent);
APPTRAVERSE_REGISTER(ChatBindingChangedEvent);
APPTRAVERSE_REGISTER(DraftChangedEvent);
APPTRAVERSE_REGISTER(ScrollChangedEvent);
APPTRAVERSE_REGISTER(MessageAddedEvent);

}  // namespace

// ChatRoom Apply & CanApply
bool ChatRoom::CanApply(MessageAddedEvent const& event) const {
  if (event.message.id.origin_uid.empty() ||
      event.message.id.origin_sequence == 0 ||
      event.message.timestamp_us == 0 ||
      event.message.text.empty()) {
    return false;
  }
  return true;
}

void ChatRoom::Apply(MessageAddedEvent const& event) {
  messages.push_back(event.message);
  NoteMaterializedChange();
}

// ChatEntry Apply
void ChatEntry::Apply(ChatBindingChangedEvent const& event) {
  peer_link = event.peer_link;
  room = event.room;
  NoteMaterializedChange();
}

void ChatEntry::Apply(DraftChangedEvent const& event) {
  draft = event.text;
  NoteMaterializedChange();
}

void ChatEntry::Apply(ScrollChangedEvent const& event) {
  scroll = event.scroll;
  NoteMaterializedChange();
}

// ChatWorkspace Apply
void ChatWorkspace::Apply(ChatEntryAddedEvent const& event) {
  chats.push_back(event.entry);
  NoteMaterializedChange();
}

bool ChatWorkspace::CanApply(ChatSelectedEvent const& event) const {
  if (!event.entry_id.is_valid()) {
    return false;
  }
  for (auto const& entry : chats) {
    if (entry.is_valid() && entry.id() == event.entry_id) {
      return true;
    }
  }
  return false;
}

void ChatWorkspace::Apply(ChatSelectedEvent const& event) {
  selected_chat_id = event.entry_id;
  NoteMaterializedChange();
}

void ChatWorkspace::Apply(LocalEndpointBoundEvent const& event) {
  local_endpoint_uid = event.endpoint_uid;
  NoteMaterializedChange();
}

bool ChatWorkspace::CanApply(MessageSequenceReservedEvent const& event) const {
  return event.reserved_sequence == next_message_sequence &&
         event.reserved_sequence != 0 &&
         event.reserved_sequence !=
             std::numeric_limits<std::uint64_t>::max();
}

void ChatWorkspace::Apply(MessageSequenceReservedEvent const& event) {
  next_message_sequence = event.reserved_sequence + 1;
  NoteMaterializedChange();
}

void ChatWorkspace::Apply(DesktopBoundsChangedEvent const& event) {
  desktop_bounds = event.bounds;
  NoteMaterializedChange();
}

void EnsureChatDemoModelRegistration() {
  apptraverse::EnsureObjectRegistration();
}

}  // namespace apptraverse::example::chat_demo
