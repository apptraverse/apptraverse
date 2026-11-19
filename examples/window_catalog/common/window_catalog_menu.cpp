#include "window_catalog_menu.h"
#include <algorithm>

namespace apptraverse {

void CatalogMenuDefinition::MaterializeLanguage(int language) {
  for (auto& group : groups) {
    group.label->MaterializeLanguage(language);
    for (auto& item : group.items) item.label->MaterializeLanguage(language);
  }
  for (auto& item : layout_items) item.label->MaterializeLanguage(language);
  for (auto& text : shell_strings) text->MaterializeLanguage(language);
}

void CatalogWindowDisplayMenuDefinition::MaterializeLanguage(int language) {
  CatalogMenuDefinition::MaterializeLanguage(language);
  for (auto& item : fullscreen_items) item.label->MaterializeLanguage(language);
}

bool CatalogUiState::IsWindowFullscreen(ae::ObjId window_id) const {
  return std::find(fullscreen_windows.begin(), fullscreen_windows.end(), window_id)
      != fullscreen_windows.end();
}

void CatalogUiState::RequestWindowFullscreenFromUi(ae::ObjId window_id,
                                                   bool fullscreen) {
  if (menu->GetClassId() != CatalogWindowDisplayMenuDefinition::kClassId) return;
  if (!AcceptsWindowObservation(window_id)) return;
  // A queued native command can outlive a child or its row. Membership/open
  // status is resolved on the model thread without materializing lazy windows.
  if (window_id != catalog->persisted.main_window.id()) {
    auto const* holder = catalog->FindHolderForWindow(window_id);
    if (!holder || !catalog->IsHolderWindowOpen(*holder)) return;
  }
  if (IsWindowFullscreen(window_id) == fullscreen) return;
  auto event = SetCatalogWindowFullscreenEvent::ptr::Create(ae::CreateWith{*domain});
  event->window_id = window_id;
  event->fullscreen = fullscreen;
  CommitLocal(std::move(event));
}

void CatalogUiState::ToggleWindowFullscreenFromUi(ae::ObjId window_id) {
  RequestWindowFullscreenFromUi(window_id, !IsWindowFullscreen(window_id));
}

void CatalogUiState::Apply(SetCatalogWindowFullscreenEvent const& event) {
  std::erase(fullscreen_windows, event.window_id);
  if (event.fullscreen) fullscreen_windows.push_back(event.window_id);
  NoteMaterializedChange();
}

void CatalogUiState::ForgetWindowState(ae::ObjId window_id) {
  std::erase(fullscreen_windows, window_id);
}

std::set<ae::ObjId> CatalogUiState::WindowStateIds() const {
  auto ids = ui::UiDeviceState::WindowStateIds();
  ids.insert(fullscreen_windows.begin(), fullscreen_windows.end());
  return ids;
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
  if (item.action == CatalogMenuAction::kLayoutFlow)
    return static_cast<int>(catalog->persisted.main_window->layout->EffectiveRoot().flow) == item.value;
  if (item.action == CatalogMenuAction::kResetLayout) return false;
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
  locale->SubmitLanguageIndexFromUi(item.value);
}

void CatalogUiState::ExecuteLayoutCommand(unsigned index) {
  if (index >= menu->layout_items.size()) return;
  auto const item = menu->layout_items[index];
  auto layout = catalog->persisted.main_window->layout;
  if (item.action == CatalogMenuAction::kResetLayout) {
    layout->ClearOverride();
  } else if (item.action == CatalogMenuAction::kLayoutFlow) {
    // Read the current semantic definition on the model thread. Native input
    // supplies only a command index and never an obsolete copy of the tree.
    auto changed = layout->EffectiveRoot();
    changed.flow = static_cast<ui::UiLayoutFlow>(item.value);
    layout->SetOverride(std::move(changed));
  }
}

APPTRAVERSE_REGISTER(CatalogUiState);
APPTRAVERSE_REGISTER(CatalogMenuDefinition);
APPTRAVERSE_REGISTER(CatalogWindowDisplayMenuDefinition);
APPTRAVERSE_REGISTER(SetCatalogWindowFullscreenEvent);
void EnsureCatalogMenuRegistration() {
  (void)&g_apptraverse_registrar_CatalogUiState;
  (void)&g_apptraverse_registrar_CatalogMenuDefinition;
  (void)&g_apptraverse_registrar_CatalogWindowDisplayMenuDefinition;
  (void)&g_apptraverse_registrar_SetCatalogWindowFullscreenEvent;
}
}  // namespace apptraverse
