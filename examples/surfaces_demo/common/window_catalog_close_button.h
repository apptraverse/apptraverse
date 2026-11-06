#ifndef APPTRAVERSE_WINDOW_CATALOG_CLOSE_BUTTON_H_
#define APPTRAVERSE_WINDOW_CATALOG_CLOSE_BUTTON_H_

// CatalogCloseButton is defined in window_catalog_model.h so CatalogWindow can
// reference it from the window prefab graph.
#include "window_catalog_model.h"

namespace apptraverse::example::surfaces {

void EnsureCatalogCloseButtonRegistration();

}  // namespace apptraverse::example::surfaces

#endif  // APPTRAVERSE_WINDOW_CATALOG_CLOSE_BUTTON_H_
