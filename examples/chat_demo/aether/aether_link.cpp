#include "aether_link.h"

#include "apptraverse/object_macros.h"
#include "chat_model.h"

namespace apptraverse::example::chat_demo {
namespace {

APPTRAVERSE_REGISTER(AetherLink);

}  // namespace

void EnsureAetherLinkRegistration() {
  apptraverse::EnsureObjectRegistration();
  EnsureChatDemoModelRegistration();
  static bool registered = false;
  if (!registered) {
    registered = true;
    (void)AetherLink::kClassId;
  }
}

}  // namespace apptraverse::example::chat_demo
