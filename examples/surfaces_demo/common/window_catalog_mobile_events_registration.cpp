#include "window_catalog_model.h"

namespace apptraverse {

APPTRAVERSE_REGISTER(SetWindowCatalogMobileActiveEvent);
APPTRAVERSE_REGISTER(SetCatalogChildViewportEvent);

void TouchWindowCatalogMobileEventsRegistration() {
  (void)&g_apptraverse_registrar_SetWindowCatalogMobileActiveEvent;
  (void)&g_apptraverse_registrar_SetCatalogChildViewportEvent;
}

}  // namespace apptraverse
