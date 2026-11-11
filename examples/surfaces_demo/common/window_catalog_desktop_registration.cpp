#include "window_catalog_actions.h"
#include "window_catalog_close_button.h"
#include "window_catalog_model.h"

namespace apptraverse {
void TouchWindowCatalogCoreRegistration();
void TouchWindowCatalogEventsRegistration();
void TouchWindowCatalogMobileEventsRegistration();

void EnsureWindowCatalogDesktopModelRegistration() {
  example::surfaces::EnsureCatalogActionRegistration();
  TouchWindowCatalogCoreRegistration();
  TouchWindowCatalogEventsRegistration();
  // Portable USER may have been written in paged presentation mode.
  TouchWindowCatalogMobileEventsRegistration();
  example::surfaces::EnsureCatalogCloseButtonRegistration();
}
}  // namespace apptraverse
