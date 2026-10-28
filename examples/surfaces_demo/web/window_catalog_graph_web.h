#ifndef APPTRAVERSE_WINDOW_CATALOG_GRAPH_WEB_H_
#define APPTRAVERSE_WINDOW_CATALOG_GRAPH_WEB_H_

#include "aether-objects/obj/domain.h"

namespace apptraverse {

class WindowCatalog;

WindowCatalog::ptr BuildWebWindowCatalogGraph(ae::Domain& domain);
void EnsureWebWindowCatalogGraphRegistration();

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_CATALOG_GRAPH_WEB_H_
