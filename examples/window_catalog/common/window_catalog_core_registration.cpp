#include "window_catalog_model.h"

namespace apptraverse {

APPTRAVERSE_REGISTER(WindowCatalog);
APPTRAVERSE_REGISTER(WindowHolder);
APPTRAVERSE_REGISTER(CatalogWindow);
APPTRAVERSE_REGISTER(CatalogListContainer);

void TouchWindowCatalogCoreRegistration() {
  (void)&g_apptraverse_registrar_WindowCatalog;
  (void)&g_apptraverse_registrar_WindowHolder;
  (void)&g_apptraverse_registrar_CatalogWindow;
  (void)&g_apptraverse_registrar_CatalogListContainer;
}

}  // namespace apptraverse
