#include "window_catalog_actions.h"
#include "window_catalog_close_button.h"
#include "window_catalog_model.h"

namespace apptraverse {
void TouchWindowCatalogCoreRegistration();
void TouchWindowCatalogEventsRegistration();

void EnsureWindowCatalogDesktopModelRegistration() {
  example::surfaces::EnsureCatalogActionRegistration();
  TouchWindowCatalogCoreRegistration();
  TouchWindowCatalogEventsRegistration();
  example::surfaces::EnsureCatalogCloseButtonRegistration();
}
}  // namespace apptraverse
