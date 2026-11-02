#ifndef APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_GRAPH_LINUX_H_
#define APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_GRAPH_LINUX_H_

#include "minimal_application.h"

namespace ae {
class Domain;
}

namespace apptraverse::example::minimal {

MinimalApplication::ptr BuildLinuxMinimalApplicationGraph(ae::Domain& domain);

}  // namespace apptraverse::example::minimal

#endif  // APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_GRAPH_LINUX_H_
