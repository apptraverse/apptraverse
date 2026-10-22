#include "apptraverse/surfaces/surfaces_presentation.h"

#include "surfaces_demo_win32_attach.h"

namespace apptraverse {

void AttachPlatformSurfacePresenters(Surface& surface, Application& application,
                                     ae::Domain& domain) {
  example::surfaces::AttachSurfacesDemoWin32SurfacePresenters(
      surface, application, domain);
}

}  // namespace apptraverse
