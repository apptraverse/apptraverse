#include "window_catalog_actions.h"
#include "window_catalog_close_button.h"
#include "window_catalog_model.h"

namespace apptraverse {

void TouchWindowCatalogMobileEventsRegistration();

void EnsureWindowCatalogModelRegistration() {
  EnsureWindowCatalogDesktopModelRegistration();
  TouchWindowCatalogMobileEventsRegistration();
  example::surfaces::EnsureCatalogCloseButtonRegistration();
}

}  // namespace apptraverse
