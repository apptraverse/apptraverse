#include "demo_model.h"

#include "demo_events.h"
#include "apptraverse/materialized_ops.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER_MATERIALIZED(ImmutableString);
APPTRAVERSE_REGISTER_MATERIALIZED(TextToolbar);
APPTRAVERSE_REGISTER_MATERIALIZED(ColorToolbar);
APPTRAVERSE_REGISTER_MATERIALIZED(CenterStrip);
APPTRAVERSE_REGISTER_MATERIALIZED(Window);
APPTRAVERSE_REGISTER_MATERIALIZED(PaintWindow);
APPTRAVERSE_REGISTER_MATERIALIZED(LayoutWindow);
APPTRAVERSE_REGISTER_MATERIALIZED(Application);
APPTRAVERSE_REGISTER(WindowBoundsChangedEvent);
APPTRAVERSE_REGISTER(ColorChangedEvent);

}  // namespace

void EnsureDemoRegistration() { EnsureObjectRegistration(); }

}  // namespace apptraverse
