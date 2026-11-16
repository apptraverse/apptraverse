#ifndef APPTRAVERSE_EXAMPLE_SURFACES_DEMO_WIN32_ATTACH_H_
#define APPTRAVERSE_EXAMPLE_SURFACES_DEMO_WIN32_ATTACH_H_

#include "apptraverse/surfaces/surfaces_model.h"

namespace apptraverse {
class Application;
}

namespace apptraverse::example::surfaces {

void AttachSurfacesDemoWin32SurfacePresenters(Surface& surface,
                                                Application& application,
                                                ae::Domain& domain);

}  // namespace apptraverse::example::surfaces

#endif  // APPTRAVERSE_EXAMPLE_SURFACES_DEMO_WIN32_ATTACH_H_
