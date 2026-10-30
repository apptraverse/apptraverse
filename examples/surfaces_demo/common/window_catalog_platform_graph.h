#ifndef APPTRAVERSE_WINDOW_CATALOG_PLATFORM_GRAPH_H_
#define APPTRAVERSE_WINDOW_CATALOG_PLATFORM_GRAPH_H_

#include "window_catalog_model.h"

namespace apptraverse {

// Implemented by the platform composition unit linked into the demo
// (Windows, macOS, iOS, headless, web). The shared lifecycle calls these
// and does not include a platform graph header.
WindowCatalog::ptr BuildWindowCatalogGraph(ae::Domain& domain);

void EnsureWindowCatalogGraphRegistration();

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_CATALOG_PLATFORM_GRAPH_H_
