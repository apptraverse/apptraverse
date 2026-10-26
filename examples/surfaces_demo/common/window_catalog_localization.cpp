#include "window_catalog_localization.h"

#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_localization_string.h"
#include "apptraverse/ui/ui_presented_window.h"
#include "apptraverse/ui/ui_string.h"

namespace apptraverse {

namespace {

void PersistUiStringUtf8(ui::UiString::ptr& string) {
  if (!string.is_loaded()) {
    string.Load();
  }
  string.Save();
  string.Reset();
}

ui::UiLocalizationString::ptr MakeLocalizedString(ae::Domain& domain,
                                                 Node& owner, char const* en,
                                                 char const* ru) {
  auto localized = ui::UiLocalizationString::ptr::Create(
      ae::CreateWith{domain});
  auto en_string = ui::UiString::ptr::Create(
      ae::CreateWith{domain}.with_flags(ae::ObjFlags::kUnloadedByDefault));
  auto ru_string = ui::UiString::ptr::Create(
      ae::CreateWith{domain}.with_flags(ae::ObjFlags::kUnloadedByDefault));
  localized->strings = {en_string, ru_string};
  InitializeRuntimeNode(*localized, owner);

  en_string.Load();
  en_string->utf8 = en;
  PersistUiStringUtf8(en_string);
  ru_string.Load();
  ru_string->utf8 = ru;
  PersistUiStringUtf8(ru_string);
  localized->MaterializeLanguage(0);
  return localized;
}

}  // namespace

void InstallWindowCatalogLocalization(WindowCatalog& catalog,
                                      ae::Domain& domain) {
  catalog.locale = ui::UiLocaleSettings::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*catalog.locale, catalog);

  catalog.texts.label_add =
      MakeLocalizedString(domain, catalog, "Add", "Добавить");
  catalog.texts.label_open =
      MakeLocalizedString(domain, catalog, "Open", "Открыть");
  catalog.texts.label_delete =
      MakeLocalizedString(domain, catalog, "Delete", "Удалить");
  catalog.texts.label_language =
      MakeLocalizedString(domain, catalog, "RU", "EN");
  catalog.texts.window_title_prefix =
      MakeLocalizedString(domain, catalog, "Window ", "Окно ");
}

ui::UiLocalizationString::ptr CreateMainWindowTitle(WindowCatalog& catalog,
                                                    ae::Domain& domain) {
  return MakeLocalizedString(domain, catalog, "Window Catalog",
                             "Каталог окон");
}

std::string FormatCatalogWindowTitle(ui::UiLocalizationString const& prefix,
                                     int language_index,
                                     std::uint32_t number) {
  return prefix.Resolve(language_index) + std::to_string(number);
}

void MaterializeWindowHolderTitle(WindowHolder& holder, int language_index,
                                  ui::UiLocalizationString const& prefix) {
  holder.primary_text->MaterializeLanguage(language_index);
  if (holder.window.is_loaded()) {
    auto& presented = static_cast<ui::UiPresentedWindow&>(*holder.window);
    if (presented.presenter.is_loaded()) {
      presented.presenter->title->MaterializeLanguage(language_index);
    }
  }
}

}  // namespace apptraverse
