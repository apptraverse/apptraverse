#ifndef APPTRAVERSE_SURFACES_PRESENTATION_H_
#define APPTRAVERSE_SURFACES_PRESENTATION_H_

#include "aether-objects/obj/domain.h"

namespace apptraverse {

class Surface;

// Platform host creates window + button presenter objects and wires Surface refs.
void AttachPlatformSurfacePresenters(Surface& surface, ae::Domain& domain);

}  // namespace apptraverse

#endif  // APPTRAVERSE_SURFACES_PRESENTATION_H_
