#include "window_catalog_lifecycle.h"

#include "window_catalog_model.h"

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/ui/ui_presented_window_lifecycle.h"

namespace apptraverse {

void PrepareWindowCatalogForRun(WindowCatalog& catalog) {
  ui::LoadPresentedWindow(catalog.main_window);
  catalog.list.Load();
  if (!catalog.list) {
    WriteFatalStderr("fatal: WindowCatalog required list missing\n");
    std::abort();
  }
  catalog.locale.Load();
  if (!catalog.locale) {
    WriteFatalStderr("fatal: WindowCatalog required locale missing\n");
    std::abort();
  }
  auto& chrome_presenters = catalog.presenters;
  chrome_presenters.list_presenter.Load();
  chrome_presenters.add_button_presenter.Load();
  chrome_presenters.open_button_presenter.Load();
  chrome_presenters.delete_button_presenter.Load();
  chrome_presenters.language_button_presenter.Load();
  if (!chrome_presenters.list_presenter ||
      !chrome_presenters.add_button_presenter ||
      !chrome_presenters.open_button_presenter ||
      !chrome_presenters.delete_button_presenter ||
      !chrome_presenters.language_button_presenter) {
    WriteFatalStderr(
        "fatal: WindowCatalog required chrome presenter missing\n");
    std::abort();
  }
  auto& chrome_buttons = catalog.buttons;
  chrome_buttons.add_button.Load();
  chrome_buttons.open_button.Load();
  chrome_buttons.delete_button.Load();
  chrome_buttons.language_button.Load();
  if (!chrome_buttons.add_button || !chrome_buttons.open_button ||
      !chrome_buttons.delete_button || !chrome_buttons.language_button) {
    WriteFatalStderr("fatal: WindowCatalog required chrome button missing\n");
    std::abort();
  }
  auto& localized_texts = catalog.texts;
  localized_texts.label_add.Load();
  localized_texts.label_open.Load();
  localized_texts.label_delete.Load();
  localized_texts.label_language.Load();
  localized_texts.window_title_prefix.Load();
  if (!localized_texts.label_add || !localized_texts.label_open ||
      !localized_texts.label_delete || !localized_texts.label_language ||
      !localized_texts.window_title_prefix) {
    WriteFatalStderr("fatal: WindowCatalog required localized text missing\n");
    std::abort();
  }
  int const language = catalog.locale->language_index();
  catalog.MaterializeCatalogStrings(language);
  for (auto const& row : catalog.list->rows) {
    auto* holder = static_cast<WindowHolder*>(&*row);
    if (!holder->primary_text.is_loaded()) {
      holder->primary_text.Load();
    }
    catalog.MaterializeHolderListText(*holder, language);
  }
  catalog.RefreshChromeButtonEnabledState();
  RestoreLoadedCatalogWindows(catalog);
}

}  // namespace apptraverse
