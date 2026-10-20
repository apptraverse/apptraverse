#ifndef APPTRAVERSE_SURFACES_RUNTIME_EXECUTION_H_
#define APPTRAVERSE_SURFACES_RUNTIME_EXECUTION_H_

#include "apptraverse/surfaces/surfaces_model.h"

namespace apptraverse {

// Persistent graph invariants after full load, before runtime-only bindings.
void VerifySurfacesPersistentGraph(Application const& application);

// Idempotent: verify graph, then attach host_application / platform_attach.
void WireSurfacesDemoRuntimeExecution(Application& application);

}  // namespace apptraverse

#endif  // APPTRAVERSE_SURFACES_RUNTIME_EXECUTION_H_
