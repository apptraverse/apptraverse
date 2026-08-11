#include "apptraverse/object_macros.h"

#include "apptraverse/event.h"
#include "apptraverse/node.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(Event);
APPTRAVERSE_REGISTER(Node);

}  // namespace

// Forces the static library object file (and its Registrars) to be linked.
void EnsureObjectRegistration() {}

}  // namespace apptraverse
