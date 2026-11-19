#include "window_catalog_actions.h"

namespace apptraverse::example::surfaces {

void CatalogLanguageButton::HandlePushButtonClicked() {
  if (!catalog.is_loaded()) {
    catalog.Load();
  }
  WindowCatalog& cat = *catalog;
  cat.persisted.locale.Load();
  int const next = 1 - cat.persisted.locale->language_index();
  cat.MaterializeCatalogStrings(next);
  cat.persisted.locale->SubmitLanguageIndexFromUi(next);
}

}  // namespace apptraverse::example::surfaces
