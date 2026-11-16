#ifndef APPTRAVERSE_WINDOW_CATALOG_GRAPH_HEADLESS_H_
#define APPTRAVERSE_WINDOW_CATALOG_GRAPH_HEADLESS_H_

#include "aether-objects/obj/domain.h"

#include "window_catalog_graph_builder.h"
#include "window_catalog_model.h"

namespace apptraverse {

WindowCatalog::ptr BuildHeadlessWindowCatalogGraph(ae::Domain& domain);

WindowCatalog::ptr BuildHeadlessWindowCatalogGraph(
    ae::Domain& domain, WindowCatalogGraphOptions options);

void EnsureHeadlessWindowCatalogGraphRegistration();

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_CATALOG_GRAPH_HEADLESS_H_
