#include "demo_model.h"

#include "demo_events.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(ImmutableString);
APPTRAVERSE_REGISTER(TextToolbar);
APPTRAVERSE_REGISTER(ColorToolbar);
APPTRAVERSE_REGISTER(Chat);
APPTRAVERSE_REGISTER(Window);
APPTRAVERSE_REGISTER(Application);
APPTRAVERSE_REGISTER(WindowBoundsChangedEvent);
APPTRAVERSE_REGISTER(ColorChangedEvent);
APPTRAVERSE_REGISTER(AddMessageEvent);

}  // namespace

void EnsureDemoRegistration() { EnsureObjectRegistration(); }

}  // namespace apptraverse
