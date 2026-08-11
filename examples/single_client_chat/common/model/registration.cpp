#include "model/registration.h"

#include <cassert>

#include "apptraverse/object_macros.h"

#include "model/app.h"
#include "model/chat.h"
#include "model/chat_events.h"
#include "model/chat_peer_events.h"
#include "model/chat_peer_set.h"
#include "model/chat_presenter.h"
#include "model/client.h"
#include "model/window.h"
#include "model/window_changed_event.h"
#include "model/window_presenter.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(Client);
APPTRAVERSE_REGISTER(JoinClientEvent);
APPTRAVERSE_REGISTER(AddMessageEvent);
APPTRAVERSE_REGISTER(Chat);
APPTRAVERSE_REGISTER(ChatPeerSet);
APPTRAVERSE_REGISTER(AddChatPeerEvent);
APPTRAVERSE_REGISTER(ChatPresenter);
APPTRAVERSE_REGISTER(Window);
APPTRAVERSE_REGISTER(WindowChangedEvent);
APPTRAVERSE_REGISTER(WindowPresenter);
APPTRAVERSE_REGISTER(App);

}  // namespace

void Window::Apply(WindowChangedEvent const& event) {
  (void)event;
  assert(false && "Platform Window must implement Apply(WindowChangedEvent)");
}

void EnsureSingleClientChatRegistration() {}

}  // namespace apptraverse
