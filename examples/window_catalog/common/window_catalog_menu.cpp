#include "window_catalog_menu.h"

namespace apptraverse {

void CatalogMenuDefinition::MaterializeLanguage(int language) {
  for (auto& group : groups) {
    group.label->MaterializeLanguage(language);
    for (auto& item : group.items) item.label->MaterializeLanguage(language);
  }
}

void PrepareCatalogMenuForRun(WindowCatalog& catalog) {
  catalog.persisted.locale.Load();
  auto& state = static_cast<CatalogUiState&>(*catalog.persisted.device_state);
  state.menu.Load();
  state.menu->MaterializeLanguage(catalog.persisted.locale->language_index());
}

bool CatalogUiState::MenuItemSelected(CatalogMenuItem const& item) const {
  if (item.action == CatalogMenuAction::kLanguage)
    return catalog->persisted.locale->language_index() == item.value;
  int const selected = appearance.theme == ui::UiThemeMode::kAuto ? 0 :
      appearance.theme == ui::UiThemeMode::kLight ? 1 : appearance.soften_dark ? 3 : 2;
  return selected == item.value;
}

void CatalogUiState::ExecuteMenuCommand(unsigned group, unsigned index) {
  // Validate external intent against the authored menu, not a native command
  // table. Presenters contain no separate language/theme behavior.
  if (group >= menu->groups.size() || index >= menu->groups[group].items.size()) return;
  auto const item = menu->groups[group].items[index];
  if (item.action == CatalogMenuAction::kTheme) {
    SetAppearance({item.value == 0 ? ui::UiThemeMode::kAuto :
        item.value == 1 ? ui::UiThemeMode::kLight : ui::UiThemeMode::kDark, item.value == 3});
    return;
  }
  catalog->MaterializeCatalogStrings(item.value);
  menu->MaterializeLanguage(item.value);
  auto locale = catalog->persisted.locale;
  if (locale->language_index() == item.value) return;
  auto event = ui::SetUiLanguageIndexEvent::ptr::Create(ae::CreateWith{*domain});
  event->SetObjectScope(locale->object_scope());
  event->language_index = item.value;
  locale->Commit(event);
}

APPTRAVERSE_REGISTER(CatalogUiState);
APPTRAVERSE_REGISTER(CatalogMenuDefinition);
void EnsureCatalogMenuRegistration() {
  (void)&g_apptraverse_registrar_CatalogUiState;
  (void)&g_apptraverse_registrar_CatalogMenuDefinition;
}
}  // namespace apptraverse
