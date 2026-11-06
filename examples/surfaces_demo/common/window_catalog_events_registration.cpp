#include "window_catalog_model.h"

namespace apptraverse {

APPTRAVERSE_REGISTER(AddWindowCatalogEntryEvent);
APPTRAVERSE_REGISTER(RemoveWindowCatalogEntryEvent);
APPTRAVERSE_REGISTER(SetWindowCatalogSelectionEvent);
APPTRAVERSE_REGISTER(SetWindowCatalogOpenEvent);
APPTRAVERSE_REGISTER(SetWindowCatalogDesktopZOrderEvent);
APPTRAVERSE_REGISTER(SetWindowCatalogMobileActiveEvent);
APPTRAVERSE_REGISTER(SetWindowCatalogMainClientViewportEvent);

void TouchWindowCatalogEventsRegistration() {
  (void)&g_apptraverse_registrar_AddWindowCatalogEntryEvent;
  (void)&g_apptraverse_registrar_RemoveWindowCatalogEntryEvent;
  (void)&g_apptraverse_registrar_SetWindowCatalogSelectionEvent;
  (void)&g_apptraverse_registrar_SetWindowCatalogOpenEvent;
  (void)&g_apptraverse_registrar_SetWindowCatalogDesktopZOrderEvent;
  (void)&g_apptraverse_registrar_SetWindowCatalogMobileActiveEvent;
  (void)&g_apptraverse_registrar_SetWindowCatalogMainClientViewportEvent;
}

}  // namespace apptraverse
