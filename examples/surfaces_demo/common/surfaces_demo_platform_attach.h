#ifndef APPTRAVERSE_SURFACES_DEMO_PLATFORM_ATTACH_H_
#define APPTRAVERSE_SURFACES_DEMO_PLATFORM_ATTACH_H_

#include "apptraverse/surfaces/surface_platform_attach.h"

namespace apptraverse {

void AttachSurfacesDemoPlatformPresenters(Surface& surface,
                                            Application& application,
                                            ae::Domain& domain);

inline void WireSurfacesDemoPlatformBindings(Surfaces& surfaces) {
  surfaces.platform_attach = AttachSurfacesDemoPlatformPresenters;
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_SURFACES_DEMO_PLATFORM_ATTACH_H_
