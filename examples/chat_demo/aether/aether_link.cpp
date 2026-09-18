#include "aether_link.h"

#include "apptraverse/object_macros.h"

namespace apptraverse::example::chat_demo {
namespace {

APPTRAVERSE_REGISTER(AetherLink);

}  // namespace

void EnsureAetherLinkRegistration() {
  apptraverse::EnsureObjectRegistration();
  static bool registered = false;
  if (!registered) {
    registered = true;
    (void)AetherLink::kClassId;
  }
}

}  // namespace apptraverse::example::chat_demo
