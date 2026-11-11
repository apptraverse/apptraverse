#include "window_catalog_actions.h"
#include "window_catalog_close_button.h"
#include "window_catalog_model.h"

namespace apptraverse {

void EnsureWindowCatalogModelRegistration() {
  EnsureWindowCatalogDesktopModelRegistration();
  example::surfaces::EnsureCatalogCloseButtonRegistration();
}

}  // namespace apptraverse
