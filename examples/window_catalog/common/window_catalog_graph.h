#ifndef APPTRAVERSE_WINDOW_CATALOG_GRAPH_H_
#define APPTRAVERSE_WINDOW_CATALOG_GRAPH_H_

#include "window_catalog_model.h"

namespace apptraverse {

WindowCatalog::ptr BuildWindowCatalogGraph(ae::Domain& domain);

// On Windows and Linux this is the startup registration entry point, including core
// and UI dependencies. Call before loading or building the application graph.
void EnsureWindowCatalogGraphRegistration();

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_CATALOG_GRAPH_H_
