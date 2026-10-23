#include "window_catalog_model.h"

namespace apptraverse {

APPTRAVERSE_REGISTER(WindowCatalog);
APPTRAVERSE_REGISTER(WindowHolder);
APPTRAVERSE_REGISTER(CatalogWindow);
APPTRAVERSE_REGISTER(CatalogListContainer);
APPTRAVERSE_REGISTER(AddWindowCatalogEntryEvent);
APPTRAVERSE_REGISTER(RemoveWindowCatalogEntryEvent);
APPTRAVERSE_REGISTER(SetWindowCatalogSelectionEvent);

void EnsureWindowCatalogModelRegistration() {
  (void)&g_apptraverse_registrar_WindowCatalog;
  (void)&g_apptraverse_registrar_WindowHolder;
  (void)&g_apptraverse_registrar_CatalogWindow;
  (void)&g_apptraverse_registrar_CatalogListContainer;
  (void)&g_apptraverse_registrar_AddWindowCatalogEntryEvent;
  (void)&g_apptraverse_registrar_RemoveWindowCatalogEntryEvent;
  (void)&g_apptraverse_registrar_SetWindowCatalogSelectionEvent;
}

}  // namespace apptraverse
