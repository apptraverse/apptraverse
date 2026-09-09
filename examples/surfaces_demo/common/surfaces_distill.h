#ifndef APPTRAVERSE_SURFACES_DISTILL_H_
#define APPTRAVERSE_SURFACES_DISTILL_H_

#include "aether-objects/obj/domain.h"

#include "surfaces_model.h"

namespace apptraverse {

// Dev-only initial graph. Production load path never calls this.
Application::ptr BuildSurfacesGraph(ae::Domain& domain);

}  // namespace apptraverse

#endif  // APPTRAVERSE_SURFACES_DISTILL_H_
