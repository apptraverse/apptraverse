#include "chat_model.h"

#include "apptraverse/object/object_macros.h"
#include "apptraverse/runtime_lifecycle.h"
#include "chat_events.h"

namespace chat {
namespace {

APPTRAVERSE_REGISTER(ImmutableString);
APPTRAVERSE_REGISTER(ChatClient);
APPTRAVERSE_REGISTER(ChatFeedItem);
APPTRAVERSE_REGISTER(ChatRoom);
APPTRAVERSE_REGISTER(ChatApplication);
APPTRAVERSE_REGISTER(ClientAddedEvent);
APPTRAVERSE_REGISTER(ChatMessageEvent);
APPTRAVERSE_REGISTER(PresenceChangedEvent);
APPTRAVERSE_REGISTER(PresenceMonitoringStartedEvent);

}  // namespace

void EnsureChatRegistration() { apptraverse::EnsureObjectRegistration(); }

}  // namespace chat
