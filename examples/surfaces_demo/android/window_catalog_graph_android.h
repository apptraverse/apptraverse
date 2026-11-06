#ifndef APPTRAVERSE_EXAMPLE_SURFACES_WINDOW_CATALOG_GRAPH_ANDROID_H_
#define APPTRAVERSE_EXAMPLE_SURFACES_WINDOW_CATALOG_GRAPH_ANDROID_H_

#include "aether-objects/obj/domain.h"

#include "window_catalog_model.h"

namespace apptraverse {

WindowCatalog::ptr BuildWindowCatalogGraph(ae::Domain& domain);

void EnsureWindowCatalogAndroidGraphRegistration();

}  // namespace apptraverse

#endif  // APPTRAVERSE_EXAMPLE_SURFACES_WINDOW_CATALOG_GRAPH_ANDROID_H_
