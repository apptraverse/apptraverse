#include "model/chat_component_registration.h"

#include "apptraverse/object_macros.h"

#include "model/chat.h"
#include "model/chat_presenter.h"
#include "model/chat_events.h"
#include "model/chat_peer_events.h"
#include "model/chat_peer_set.h"
#include "model/chat_room_local_state.h"
#include "model/client.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(Client);
APPTRAVERSE_REGISTER(JoinClientEvent);
APPTRAVERSE_REGISTER(AddMessageEvent);
APPTRAVERSE_REGISTER(Chat);
APPTRAVERSE_REGISTER(ChatPeerSet);
APPTRAVERSE_REGISTER(AddChatPeerEvent);
APPTRAVERSE_REGISTER(ChatRoomLocalState);

}  // namespace

void EnsureChatComponentRegistration() {}

}  // namespace apptraverse