#include "chat_model.h"

#include "chat_events.h"
#include "apptraverse/object_macros.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(ImmutableString);
APPTRAVERSE_REGISTER(ChatClient);
APPTRAVERSE_REGISTER(ChatFeedItem);
APPTRAVERSE_REGISTER(ChatRoom);
APPTRAVERSE_REGISTER(LocalAetherIdentity);
APPTRAVERSE_REGISTER(Application);
APPTRAVERSE_REGISTER(JoinEvent);
APPTRAVERSE_REGISTER(ChatMessageEvent);

}  // namespace

void EnsureChatRegistration() { EnsureObjectRegistration(); }

}  // namespace apptraverse
