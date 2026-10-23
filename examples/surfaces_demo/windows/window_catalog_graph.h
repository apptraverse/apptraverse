#ifndef APPTRAVERSE_WINDOW_CATALOG_GRAPH_H_
#define APPTRAVERSE_WINDOW_CATALOG_GRAPH_H_

#include "window_catalog_model.h"

namespace apptraverse {

WindowCatalog::ptr BuildWindowCatalogGraph(ae::Domain& domain);

void EnsureWindowCatalogGraphRegistration();

void EnsureWindowCatalogControlLinks(WindowCatalog& catalog);

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_CATALOG_GRAPH_H_
