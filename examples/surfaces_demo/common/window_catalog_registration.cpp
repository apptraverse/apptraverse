#include "window_catalog_actions.h"
#include "window_catalog_close_button.h"
#include "window_catalog_model.h"

namespace apptraverse {

void TouchWindowCatalogCoreRegistration();
void TouchWindowCatalogEventsRegistration();

void EnsureWindowCatalogModelRegistration() {
  example::surfaces::EnsureCatalogActionRegistration();
  example::surfaces::EnsureCatalogCloseButtonRegistration();
  TouchWindowCatalogCoreRegistration();
  TouchWindowCatalogEventsRegistration();
}

}  // namespace apptraverse
