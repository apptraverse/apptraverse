#include "window_catalog_close_button.h"

#include "apptraverse/object_macros.h"

namespace apptraverse::example::surfaces {
namespace {

WindowCatalog& LoadedCatalog(WindowCatalog::ptr& catalog) {
  if (!catalog.is_loaded()) {
    catalog.Load();
  }
  return *catalog;
}

}  // namespace

void CatalogCloseButton::HandlePushButtonClicked() {
  WindowCatalog& cat = LoadedCatalog(catalog);
  cat.UserCloseWindow(window.id());
}

APPTRAVERSE_REGISTER(CatalogCloseButton);

void EnsureCatalogCloseButtonRegistration() {
  (void)&g_apptraverse_registrar_CatalogCloseButton;
}

}  // namespace apptraverse::example::surfaces
