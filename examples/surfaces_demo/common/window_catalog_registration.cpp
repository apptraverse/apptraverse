#include "window_catalog_actions.h"
#include "window_catalog_model.h"

namespace apptraverse {

APPTRAVERSE_REGISTER(WindowCatalog);
APPTRAVERSE_REGISTER(WindowHolder);
APPTRAVERSE_REGISTER(CatalogWindow);
APPTRAVERSE_REGISTER(CatalogListContainer);
APPTRAVERSE_REGISTER(AddWindowCatalogEntryEvent);
APPTRAVERSE_REGISTER(RemoveWindowCatalogEntryEvent);
APPTRAVERSE_REGISTER(SetWindowCatalogSelectionEvent);
APPTRAVERSE_REGISTER(SetWindowCatalogOpenEvent);
APPTRAVERSE_REGISTER(SetWindowCatalogDesktopZOrderEvent);

void EnsureWindowCatalogModelRegistration() {
  example::surfaces::EnsureCatalogActionRegistration();
  (void)&g_apptraverse_registrar_WindowCatalog;
  (void)&g_apptraverse_registrar_WindowHolder;
  (void)&g_apptraverse_registrar_CatalogWindow;
  (void)&g_apptraverse_registrar_CatalogListContainer;
  (void)&g_apptraverse_registrar_AddWindowCatalogEntryEvent;
  (void)&g_apptraverse_registrar_RemoveWindowCatalogEntryEvent;
  (void)&g_apptraverse_registrar_SetWindowCatalogSelectionEvent;
  (void)&g_apptraverse_registrar_SetWindowCatalogOpenEvent;
  (void)&g_apptraverse_registrar_SetWindowCatalogDesktopZOrderEvent;
}

}  // namespace apptraverse
