#ifndef APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_GRAPH_H_
#define APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_GRAPH_H_

#include "aether-objects/obj/domain.h"

#include "minimal_application.h"

namespace apptraverse::example::minimal {

MinimalApplication::ptr BuildMinimalApplicationGraph(ae::Domain& domain);

}  // namespace apptraverse::example::minimal

#endif  // APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_GRAPH_H_
