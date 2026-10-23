#include "window_catalog_localization.h"

#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_localization_string.h"
#include "apptraverse/ui/ui_string.h"

namespace apptraverse {
namespace {

void PersistUiStringUtf8(ui::UiString::ptr& string) {
  if (!string.is_valid()) {
    return;
  }
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
  InitializeRuntimeNode(*localized, owner);
  auto en_string = ui::UiString::ptr::Create(
      ae::CreateWith{domain}.with_flags(ae::ObjFlags::kUnloadedByDefault));
  auto ru_string = ui::UiString::ptr::Create(
      ae::CreateWith{domain}.with_flags(ae::ObjFlags::kUnloadedByDefault));
  localized->strings = {en_string, ru_string};

  en_string.Load();
  en_string->utf8 = en;
  PersistUiStringUtf8(en_string);
  ru_string.Load();
  ru_string->utf8 = ru;
  PersistUiStringUtf8(ru_string);
  localized->MaterializeLanguage(0);
  return localized;
}

void RepairLocalizationSlot(ui::UiLocalizationString::ptr& localized,
                            char const* en, char const* ru) {
  if (!localized) {
    return;
  }
  if (localized->strings.size() < 2) {
    return;
  }
  auto repair_slot = [&](std::size_t slot, char const* expected) {
    auto& entry = localized->strings[slot];
    if (!entry.is_valid()) {
      return;
    }
    bool const needs_repair = [&] {
      if (!entry.is_loaded()) {
        entry.Load();
      }
      if (entry->utf8.empty()) {
        entry->utf8 = expected;
        PersistUiStringUtf8(entry);
        return true;
      }
      entry.Reset();
      return false;
    }();
    (void)needs_repair;
  };
  repair_slot(0, en);
  repair_slot(1, ru);
}

void RepairHolderPrimaryText(WindowHolder& holder,
                             WindowCatalog& catalog) {
  if (!holder.primary_text.is_valid() ||
      !catalog.texts.window_title_prefix.is_valid()) {
    return;
  }
  if (!holder.primary_text.is_loaded()) {
    holder.primary_text.Load();
  }
  ui::UiLocalizationString& localized = *holder.primary_text;
  if (localized.strings.size() < 2) {
    return;
  }
  auto prefix_utf8 = [&](std::size_t slot, char const* fallback) {
    ui::UiLocalizationString& prefix = *catalog.texts.window_title_prefix;
    if (slot >= prefix.strings.size() || !prefix.strings[slot].is_valid()) {
      return std::string(fallback);
    }
    ui::UiString::ptr& prefix_entry = prefix.strings[slot];
    if (!prefix_entry.is_loaded()) {
      prefix_entry.Load();
    }
    if (prefix_entry->utf8.empty()) {
      return std::string(fallback);
    }
    return prefix_entry->utf8;
  };
  auto repair_slot = [&](std::size_t slot, int language_index,
                         char const* prefix_fallback) {
    ui::UiString::ptr& entry = localized.strings[slot];
    if (!entry.is_valid()) {
      return;
    }
    std::string const expected =
        prefix_utf8(static_cast<std::size_t>(language_index), prefix_fallback) +
        std::to_string(holder.window_number);
    if (!entry.is_loaded()) {
      entry.Load();
    }
    if (entry->utf8.empty() || entry->utf8 != expected) {
      entry->utf8 = expected;
      PersistUiStringUtf8(entry);
    }
  };
  repair_slot(0, 0, "Window ");
  repair_slot(1, 1, "\xD0\x9E\xD0\xBA\xD0\xBD\xD0\xBE ");
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

void AttachMainWindowTitle(WindowCatalog& catalog, ae::Domain& domain) {
  if (!catalog.main_window_presenter) {
    return;
  }
  auto title = MakeLocalizedString(domain, catalog, "Window Catalog",
                                   "Каталог окон");
  catalog.main_window_presenter->title = title;
}

void RepairWindowCatalogEmbeddedStrings(WindowCatalog& catalog) {
  RepairLocalizationSlot(catalog.texts.label_add, "Add", "Добавить");
  RepairLocalizationSlot(catalog.texts.label_open, "Open", "Открыть");
  RepairLocalizationSlot(catalog.texts.label_delete, "Delete", "Удалить");
  RepairLocalizationSlot(catalog.texts.label_language, "RU", "EN");
  RepairLocalizationSlot(catalog.texts.window_title_prefix, "Window ", "Окно ");
  if (catalog.main_window_presenter) {
    if (!catalog.main_window_presenter->title.is_valid()) {
      AttachMainWindowTitle(catalog, *catalog.domain);
    } else {
      RepairLocalizationSlot(catalog.main_window_presenter->title,
                             "Window Catalog", "Каталог окон");
    }
  }
  if (catalog.list) {
    for (auto const& row : catalog.list->rows) {
      if (!row) {
        continue;
      }
      RepairHolderPrimaryText(*static_cast<WindowHolder*>(&*row), catalog);
    }
  }
  int const language = catalog.locale ? catalog.locale->language_index() : 0;
  catalog.MaterializeCatalogStrings(language);
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
