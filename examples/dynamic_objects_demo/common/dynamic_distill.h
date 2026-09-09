#ifndef APPTRAVERSE_DYNAMIC_DISTILL_H_
#define APPTRAVERSE_DYNAMIC_DISTILL_H_

#include "aether-objects/obj/domain.h"

#include "dynamic_model.h"

namespace apptraverse {

// Dev-only initial graph construction. Link only into distillation builds and
// tests that need a fresh fixture. Load-only executables must not link this.
Application::ptr BuildDynamicObjectsGraph(ae::Domain& domain);

}  // namespace apptraverse

#endif  // APPTRAVERSE_DYNAMIC_DISTILL_H_
