#include "window_catalog_actions.h"
#include "window_catalog_close_button.h"
#include "window_catalog_model.h"

namespace apptraverse {

void EnsureWindowCatalogDesktopModelRegistration() {
  EnsureWindowCatalogNativeMenuModelRegistration();
  example::surfaces::EnsureCatalogActionRegistration();
}

void EnsureWindowCatalogModelRegistration() {
  EnsureWindowCatalogDesktopModelRegistration();
}

}  // namespace apptraverse
