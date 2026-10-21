#pragma once

#include "apptraverse/surfaces/surfaces_demo_revision.h"

namespace apptraverse {

class Surface;

#if APPTRAVERSE_SURFACE_DEMO_REVISION == 1
void SurfacesDemoOnSurfaceAdded(Surface& surface);
#endif

}  // namespace apptraverse
