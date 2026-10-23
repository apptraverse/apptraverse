#include "window_catalog_localization.h"

#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_localization_string.h"
#include "apptraverse/ui/ui_string.h"

namespace apptraverse {
namespace {

ui::UiLocalizationString::ptr MakeLocalizedString(ae::Domain& domain,
                                                 Node& owner, char const* en,
                                                 char const* ru) {
  auto localized = ui::UiLocalizationString::ptr::Create(
      ae::CreateWith{domain});
  InitializeRuntimeNode(*localized, owner);
  auto en_string = ui::UiString::ptr::Create(
      ae::CreateWith{domain}.with_flags(ae::ObjFlags::kUnloadedByDefault));
  en_string->utf8 = en;
  auto ru_string = ui::UiString::ptr::Create(
      ae::CreateWith{domain}.with_flags(ae::ObjFlags::kUnloadedByDefault));
  ru_string->utf8 = ru;
  localized->strings = {en_string, ru_string};
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

std::string FormatCatalogWindowTitle(ui::UiLocalizationString const& prefix,
                                     int language_index,
                                     std::uint32_t number) {
  return prefix.Resolve(language_index) + std::to_string(number);
}

void MaterializeWindowHolderTitle(WindowHolder& holder, int language_index,
                                  ui::UiLocalizationString const& prefix) {
  if (!holder.primary_text) {
    return;
  }
  holder.primary_text->MaterializeLanguage(language_index);
  if (holder.window.is_loaded() && holder.window->title) {
    holder.window->title->MaterializeLanguage(language_index);
  }
}

}  // namespace apptraverse
