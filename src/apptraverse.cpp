#include "apptraverse/object_macros.h"

#include "apptraverse/app.h"
#include "apptraverse/chat.h"
#include "apptraverse/chat_entry.h"
#include "apptraverse/chat_events.h"
#include "apptraverse/chat_presenter.h"
#include "apptraverse/client.h"
#include "apptraverse/event.h"
#include "apptraverse/node.h"
#include "apptraverse/window.h"
#include "apptraverse/window_presenter.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(Event);
APPTRAVERSE_REGISTER(Node);
APPTRAVERSE_REGISTER(Client);
APPTRAVERSE_REGISTER(ChatEntry);
APPTRAVERSE_REGISTER(JoinClientEntry);
APPTRAVERSE_REGISTER(MessageEntry);
APPTRAVERSE_REGISTER(JoinClientEvent);
APPTRAVERSE_REGISTER(AddMessageEvent);
APPTRAVERSE_REGISTER(Chat);
APPTRAVERSE_REGISTER(ChatPresenter);
APPTRAVERSE_REGISTER(Window);
APPTRAVERSE_REGISTER(WindowPresenter);
APPTRAVERSE_REGISTER(App);

}  // namespace

// Forces the static library object file (and its Registrars) to be linked.
void EnsureObjectRegistration() {}

}  // namespace apptraverse
