#ifndef APPTRAVERSE_WINDOW_CATALOG_GRAPH_HEADLESS_H_
#define APPTRAVERSE_WINDOW_CATALOG_GRAPH_HEADLESS_H_

#include "aether-objects/obj/domain.h"

#include "window_catalog_model.h"

namespace apptraverse {

WindowCatalog::ptr BuildHeadlessWindowCatalogGraph(ae::Domain& domain);

void EnsureHeadlessWindowCatalogGraphRegistration();

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_CATALOG_GRAPH_HEADLESS_H_
