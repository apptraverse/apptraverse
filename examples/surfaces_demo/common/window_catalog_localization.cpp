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

ui::UiLocalizationString::ptr MakeCatalogLocalizedString(ae::Domain& domain,
                                                         Node& owner,
                                                         char const* en,
                                                         char const* ru) {
  auto localized = ui::UiLocalizationString::ptr::Create(
      ae::CreateWith{domain});
  auto en_string = ui::UiString::ptr::Create(
      ae::CreateWith{domain}.with_flags(ae::ObjFlags::kUnloadedByDefault));
  auto ru_string = ui::UiString::ptr::Create(
      ae::CreateWith{domain}.with_flags(ae::ObjFlags::kUnloadedByDefault));
  // These bootstrap resources are shared immutable catalog texts. Mark them
  // before the first Save, including the language slot that will remain lazy.
  for (ae::Obj* object : {static_cast<ae::Obj*>(localized.operator->()),
                         static_cast<ae::Obj*>(en_string.operator->()),
                         static_cast<ae::Obj*>(ru_string.operator->())}) {
    auto scope = object->object_scope();
    scope.is_constant = true;
    object->SetObjectScope(scope);
  }
  localized->strings = {en_string, ru_string};
  en_string.Load();
  en_string->utf8 = en;
  PersistUiStringUtf8(en_string);
  ru_string.Load();
  ru_string->utf8 = ru;
  PersistUiStringUtf8(ru_string);
  InitializeRuntimeNode(*localized, owner);
  localized->MaterializeLanguage(0);
  return localized;
}

}  // namespace

void InstallWindowCatalogLocalization(WindowCatalog& catalog,
                                      ae::Domain& domain) {
  catalog.persisted.locale =
      ui::UiLocaleSettings::ptr::Create(ae::CreateWith{domain});
  catalog.persisted.locale->BootstrapLanguageIndex(0);
  InitializeRuntimeNode(*catalog.persisted.locale, catalog);

  catalog.persisted.texts.label_add =
      MakeCatalogLocalizedString(domain, catalog, "Add", "Добавить");
  catalog.persisted.texts.label_open =
      CreateCatalogLocalizedString(domain, catalog, "Open", "Открыть");
  catalog.persisted.texts.label_delete =
      CreateCatalogLocalizedString(domain, catalog, "Delete", "Удалить");
  catalog.persisted.texts.label_language =
      CreateCatalogLocalizedString(domain, catalog, "RU", "EN");
  catalog.persisted.texts.window_title_prefix =
      CreateCatalogLocalizedString(domain, catalog, "Window ", "Окно ");
  catalog.persisted.texts.label_close =
      CreateCatalogLocalizedString(domain, catalog, "Close", "Закрыть");
  catalog.persisted.texts.list_row_title_format =
      CreateCatalogLocalizedString(domain, catalog, "Window {0}{1}",
                                 "Окно {0}{1}");
  catalog.persisted.texts.list_row_title_closed_suffix =
      CreateCatalogLocalizedString(domain, catalog, " (Closed)", " (закрыто)");
}

ui::UiLocalizationString::ptr CreateMainWindowTitle(WindowCatalog& catalog,
                                                    ae::Domain& domain) {
  return MakeCatalogLocalizedString(domain, catalog, "Window Catalog",
                                    "Каталог окон");
}

std::string FormatCatalogWindowTitle(ui::UiLocalizationString const& prefix,
                                     int language_index,
                                     std::uint32_t number) {
  return prefix.Resolve(language_index) + std::to_string(number);
}

ui::UiLocalizationString::ptr CreateCatalogLocalizedString(ae::Domain& domain,
                                                           Node& owner,
                                                           char const* en,
                                                           char const* ru) {
  return MakeCatalogLocalizedString(domain, owner, en, ru);
}

}  // namespace apptraverse
