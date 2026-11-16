#include "window_catalog_actions.h"

#include "apptraverse/model_object_proxy.h"
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

void CatalogAddButton::HandlePushButtonClicked() {
  LoadedCatalog(catalog).AddWindow();
}

void CatalogOpenButton::HandlePushButtonClicked() {
  LoadedCatalog(catalog).OpenSelected();
}

void CatalogDeleteButton::HandlePushButtonClicked() {
  LoadedCatalog(catalog).DeleteSelected();
}

void CatalogLanguageButton::HandlePushButtonClicked() {
  WindowCatalog& cat = LoadedCatalog(catalog);
  cat.persisted.locale.Load();
  int const next = 1 - cat.persisted.locale->language_index();
  cat.MaterializeCatalogStrings(next);
  cat.persisted.locale->SubmitLanguageIndexFromUi(next);
}

APPTRAVERSE_REGISTER(CatalogAddButton);
APPTRAVERSE_REGISTER(CatalogOpenButton);
APPTRAVERSE_REGISTER(CatalogDeleteButton);
APPTRAVERSE_REGISTER(CatalogLanguageButton);

void ReportCatalogMainViewportFromMirror(WindowCatalog& mirror_catalog,
                                         ModelObjectProxy* model_proxy) {
  if (model_proxy == nullptr ||
      !mirror_catalog.persisted.main_window.is_valid()) {
    return;
  }
  if (!mirror_catalog.persisted.main_window.is_loaded()) {
    return;
  }
  CatalogWindow const& main = *mirror_catalog.persisted.main_window;
  std::int32_t const width = main.client_width();
  std::int32_t const height = main.client_height();
  if (width <= 0 || height <= 0) {
    return;
  }
  if (width == mirror_catalog.persisted.main_client_viewport.width &&
      height == mirror_catalog.persisted.main_client_viewport.height) {
    return;
  }
  model_proxy->Invoke(mirror_catalog.obj_id,
                      &WindowCatalog::SubmitMainClientViewportFromUi, width,
                      height, 160);
}

void EnsureCatalogActionRegistration() {
  (void)&g_apptraverse_registrar_CatalogAddButton;
  (void)&g_apptraverse_registrar_CatalogOpenButton;
  (void)&g_apptraverse_registrar_CatalogDeleteButton;
  (void)&g_apptraverse_registrar_CatalogLanguageButton;
}

}  // namespace apptraverse::example::surfaces
