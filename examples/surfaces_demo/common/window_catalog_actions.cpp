#include "window_catalog_actions.h"

#include "apptraverse/object_macros.h"

namespace apptraverse::example::surfaces {
namespace {

WindowCatalog& LoadedCatalog(WindowCatalog::ptr& catalog) {
  if (!catalog.is_valid()) {
    std::abort();
  }
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
  if (!cat.list) {
    return;
  }
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
  if (!cat.locale) {
    return;
  }
  int const next = 1 - cat.locale->language_index();
  cat.MaterializeCatalogStrings(next);
  cat.locale->SubmitLanguageIndexFromUi(next);
}

APPTRAVERSE_REGISTER(CatalogAddButton);
APPTRAVERSE_REGISTER(CatalogOpenButton);
APPTRAVERSE_REGISTER(CatalogDeleteButton);
APPTRAVERSE_REGISTER(CatalogLanguageButton);

void EnsureCatalogActionRegistration() {
  (void)&g_apptraverse_registrar_CatalogAddButton;
  (void)&g_apptraverse_registrar_CatalogOpenButton;
  (void)&g_apptraverse_registrar_CatalogDeleteButton;
  (void)&g_apptraverse_registrar_CatalogLanguageButton;
}

}  // namespace apptraverse::example::surfaces
