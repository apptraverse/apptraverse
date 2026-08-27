#include "demo_model.h"

#include "demo_events.h"
#include "apptraverse/object_macros.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(ImmutableString);
APPTRAVERSE_REGISTER(TextToolbar);
APPTRAVERSE_REGISTER(ColorToolbar);
APPTRAVERSE_REGISTER(CenterStrip);
APPTRAVERSE_REGISTER(Window);
APPTRAVERSE_REGISTER(PaintWindow);
APPTRAVERSE_REGISTER(LayoutWindow);
APPTRAVERSE_REGISTER(Application);
APPTRAVERSE_REGISTER(WindowBoundsChangedEvent);
APPTRAVERSE_REGISTER(ColorChangedEvent);
APPTRAVERSE_REGISTER(TextReplacedEvent);
APPTRAVERSE_REGISTER(CenterStripAddedEvent);

}  // namespace

void EnsureDemoRegistration() { EnsureObjectRegistration(); }

}  // namespace apptraverse
