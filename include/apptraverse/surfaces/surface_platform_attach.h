#ifndef APPTRAVERSE_SURFACES_SURFACE_PLATFORM_ATTACH_H_
#define APPTRAVERSE_SURFACES_SURFACE_PLATFORM_ATTACH_H_

#include "aether-objects/obj/domain.h"

namespace apptraverse {

class Application;
class Surface;

// Runtime hook type: host/demo sets on Surfaces before model session Run.
using SurfacePlatformAttachFn = void (*)(Surface& surface, Application& application,
                                         ae::Domain& domain);

}  // namespace apptraverse

#endif  // APPTRAVERSE_SURFACES_SURFACE_PLATFORM_ATTACH_H_
