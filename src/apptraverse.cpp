#include "apptraverse/object_macros.h"

#include "apptraverse/app.h"
#include "apptraverse/chat.h"
#include "apptraverse/chat_events.h"
#include "apptraverse/chat_presenter.h"
#include "apptraverse/client.h"
#include "apptraverse/event.h"
#include "apptraverse/node.h"
#include "apptraverse/window.h"
#include "apptraverse/window_changed_event.h"
#include "apptraverse/window_presenter.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(Event);
APPTRAVERSE_REGISTER(Node);
APPTRAVERSE_REGISTER(Client);
APPTRAVERSE_REGISTER(JoinClientEvent);
APPTRAVERSE_REGISTER(AddMessageEvent);
APPTRAVERSE_REGISTER(Chat);
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

// Forces the static library object file (and its Registrars) to be linked.
void EnsureObjectRegistration() {}

}  // namespace apptraverse
