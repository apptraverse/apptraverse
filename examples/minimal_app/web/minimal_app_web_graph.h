#ifndef APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_WEB_GRAPH_H_
#define APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_WEB_GRAPH_H_

#include "aether-objects/obj/domain.h"

#include "minimal_application.h"

namespace apptraverse::example::minimal {

MinimalApplication::ptr BuildWebMinimalApplicationGraph(ae::Domain& domain);

}  // namespace apptraverse::example::minimal

#endif
