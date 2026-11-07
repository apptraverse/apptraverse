#include "window_catalog_prepare.h"

#include "window_catalog_model.h"

#include "apptraverse/ui/ui_presented_window_lifecycle.h"

namespace apptraverse {

void PrepareWindowCatalogForRun(WindowCatalog& catalog) {
  ui::LoadPresentedWindow(catalog.persisted.main_window);
  catalog.persisted.list.Load();
  catalog.persisted.locale.Load();
  auto& chrome_presenters = catalog.persisted.presenters;
  chrome_presenters.list_presenter.Load();
  chrome_presenters.add_button_presenter.Load();
  chrome_presenters.open_button_presenter.Load();
  chrome_presenters.delete_button_presenter.Load();
  chrome_presenters.language_button_presenter.Load();
  auto& chrome_buttons = catalog.persisted.buttons;
  chrome_buttons.add_button.Load();
  chrome_buttons.open_button.Load();
  chrome_buttons.delete_button.Load();
  chrome_buttons.language_button.Load();
  auto& localized_texts = catalog.persisted.texts;
  localized_texts.label_add.Load();
  localized_texts.label_open.Load();
  localized_texts.label_delete.Load();
  localized_texts.label_language.Load();
  localized_texts.window_title_prefix.Load();
  localized_texts.list_row_title_format.Load();
  localized_texts.list_row_title_closed_suffix.Load();
  if (localized_texts.label_close.is_valid()) {
    localized_texts.label_close.Load();
  }
  int const language = catalog.persisted.locale->language_index();
  catalog.MaterializeCatalogStrings(language);
  catalog.RefreshChromeButtonEnabledState();
  RestoreLoadedCatalogWindows(catalog);
}

}  // namespace apptraverse
