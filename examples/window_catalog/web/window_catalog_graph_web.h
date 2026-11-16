#ifndef APPTRAVERSE_WINDOW_CATALOG_GRAPH_WEB_H_
#define APPTRAVERSE_WINDOW_CATALOG_GRAPH_WEB_H_

#include "aether-objects/obj/domain.h"

#include "window_catalog_model.h"

namespace apptraverse {

WindowCatalog::ptr BuildWebWindowCatalogGraph(ae::Domain& domain);
void EnsureWebWindowCatalogGraphRegistration();

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_CATALOG_GRAPH_WEB_H_
