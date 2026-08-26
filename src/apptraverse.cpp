#include "apptraverse/object_macros.h"

#include "apptraverse/event.h"
#include "apptraverse/node.h"
#include "apptraverse/presenter.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(Event);
APPTRAVERSE_REGISTER(Node);
APPTRAVERSE_REGISTER(Presenter);

}  // namespace

// Forces the static library object file (and its Registrars) to be linked.
void EnsureObjectRegistration() {}

}  // namespace apptraverse
