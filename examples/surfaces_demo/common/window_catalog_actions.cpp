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
  WindowCatalog& cat = LoadedCatalog(catalog);
  cat.list.Load();
  if (cat.list->selection_index >= 0 &&
      static_cast<std::size_t>(cat.list->selection_index) <
          cat.list->rows.size()) {
    cat.OpenListRowFromUi(
        static_cast<std::size_t>(cat.list->selection_index));
    return;
  }
  cat.OpenSelected();
}

void CatalogDeleteButton::HandlePushButtonClicked() {
  LoadedCatalog(catalog).DeleteSelected();
}

void CatalogLanguageButton::HandlePushButtonClicked() {
  WindowCatalog& cat = LoadedCatalog(catalog);
  cat.locale.Load();
  int const next = 1 - cat.locale->language_index();
  cat.MaterializeCatalogStrings(next);
  cat.locale->SubmitLanguageIndexFromUi(next);
}

APPTRAVERSE_REGISTER(CatalogAddButton);
APPTRAVERSE_REGISTER(CatalogOpenButton);
APPTRAVERSE_REGISTER(CatalogDeleteButton);
APPTRAVERSE_REGISTER(CatalogLanguageButton);

void ReportCatalogMainViewportFromMirror(WindowCatalog& mirror_catalog,
                                         ModelObjectProxy* model_proxy) {
  if (model_proxy == nullptr || !mirror_catalog.main_window.is_valid()) {
    return;
  }
  if (!mirror_catalog.main_window.is_loaded()) {
    return;
  }
  CatalogWindow const& main = *mirror_catalog.main_window;
  std::int32_t const width = main.client_width();
  std::int32_t const height = main.client_height();
  if (width <= 0 || height <= 0) {
    return;
  }
  if (width == mirror_catalog.main_client_viewport_w &&
      height == mirror_catalog.main_client_viewport_h) {
    return;
  }
  model_proxy->Invoke(mirror_catalog.obj_id,
                      &WindowCatalog::SubmitMainClientViewportFromUi, width,
                      height);
}

void EnsureCatalogActionRegistration() {
  (void)&g_apptraverse_registrar_CatalogAddButton;
  (void)&g_apptraverse_registrar_CatalogOpenButton;
  (void)&g_apptraverse_registrar_CatalogDeleteButton;
  (void)&g_apptraverse_registrar_CatalogLanguageButton;
}

}  // namespace apptraverse::example::surfaces
