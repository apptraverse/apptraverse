#include "window_catalog_model.h"

namespace apptraverse {

APPTRAVERSE_REGISTER(SetWindowCatalogMobileActiveEvent);

void TouchWindowCatalogMobileEventsRegistration() {
  (void)&g_apptraverse_registrar_SetWindowCatalogMobileActiveEvent;
}

}  // namespace apptraverse
