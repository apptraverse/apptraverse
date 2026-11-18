#include "window_catalog_menu.h"
#include "window_catalog_localization.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/distill.h"

namespace apptraverse {
void ComposeCatalogMenu(ae::Domain& domain, WindowCatalog& catalog, CatalogThemeHandling theme,
                        bool web_shell, bool layout_context) {
  auto& state = static_cast<CatalogUiState&>(*catalog.persisted.device_state);
  ae::Domain::ObjectIdSequence ids{domain, ae::ObjId{2000}, ae::ObjId{9999}};
  state.catalog = WindowCatalog::ptr::MakeFromThis(&catalog);
  catalog.persisted.locale->SetObjectScope(state.object_scope());
  catalog.persisted.locale->base->SetObjectScope(state.object_scope());
  state.menu = CatalogMenuDefinition::ptr::Create(ae::CreateWith{domain});
  auto scope = state.object_scope(); scope.is_constant = true;
  state.menu->SetObjectScope(scope);
  auto text = [&](char const* en, char const* ru) {
    return CreateCatalogLocalizedString(domain, state, en, ru);
  };
  state.menu->groups.clear();
  // Preserve this application's two existing languages and endonyms. Labels
  // are normal distilled localization objects, materialized by Resolve.
  CatalogMenuGroup languages{text("&Language", "&Язык"), {}};
  languages.items = {{text("English", "English"), CatalogMenuAction::kLanguage, 0},
                     {text("Русский", "Русский"), CatalogMenuAction::kLanguage, 1}};
  state.menu->groups.push_back(std::move(languages));
  if (theme == CatalogThemeHandling::kAdapterAppliesTheme) {
    CatalogMenuGroup themes{text("&Theme", "&Тема"), {}};
    themes.items = {{text("From system", "Из системы"), CatalogMenuAction::kTheme, 0},
                    {text("Light", "Светлая"), CatalogMenuAction::kTheme, 1},
                    {text("Dark", "Тёмная"), CatalogMenuAction::kTheme, 2},
                    {text("Soft dark", "Мягкая тёмная"), CatalogMenuAction::kTheme, 3}};
    state.menu->groups.insert(state.menu->groups.begin(), std::move(themes));
  }
  if (layout_context) {
    state.menu->layout_items = {
      {text("Layout: Automatic", "Раскладка: автоматически"), CatalogMenuAction::kLayoutFlow, static_cast<int>(ui::UiLayoutFlow::kAdaptive)},
      {text("Layout: Horizontal", "Раскладка: горизонтально"), CatalogMenuAction::kLayoutFlow, static_cast<int>(ui::UiLayoutFlow::kHorizontal)},
      {text("Layout: Vertical", "Раскладка: вертикально"), CatalogMenuAction::kLayoutFlow, static_cast<int>(ui::UiLayoutFlow::kVertical)},
      {text("Reset layout to application default", "Вернуть раскладку приложения"), CatalogMenuAction::kResetLayout, 0}};
  }
  if (web_shell) {
    state.menu->shell_strings = {
      text("Previous page", "Предыдущая страница"),
      text("Next page", "Следующая страница"),
      text("Close application", "Закрыть приложение"),
      text("Page", "Страница"),
      text("Stopping…", "Остановка…"),
      text("Stopped", "Приложение остановлено"),
      text("Stop failed", "Не удалось остановить приложение"),
      text("Save failed", "Не удалось сохранить приложение")};
  }
  InitializeRuntimeNode(*state.menu, state);
  PrepareCatalogMenuForRun(catalog);
  state.CaptureBaseState();
  SaveDistilledRoot(state);
}
}  // namespace apptraverse
