#ifndef APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_GRAPH_HEADLESS_H_
#define APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_GRAPH_HEADLESS_H_

#include "aether-objects/obj/domain.h"

#include "minimal_application.h"

namespace apptraverse::example::minimal {

MinimalApplication::ptr BuildHeadlessMinimalApplicationGraph(ae::Domain& domain);

void EnsureHeadlessMinimalApplicationGraphRegistration();

}  // namespace apptraverse::example::minimal

#endif  // APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_GRAPH_HEADLESS_H_
