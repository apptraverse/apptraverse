#include "window_catalog_menu.h"
#include "window_catalog_localization.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/distill.h"

namespace apptraverse {
void ComposeCatalogMenu(ae::Domain& domain, WindowCatalog& catalog, CatalogThemeHandling theme,
                        CatalogMenuShell shell, bool layout_context, CatalogDisplayHandling display_handling) {
  auto& state = static_cast<CatalogUiState&>(*catalog.persisted.device_state);
  ae::Domain::ObjectIdSequence ids{domain, ae::ObjId{2000}, ae::ObjId{9999}};
  state.catalog = WindowCatalog::ptr::MakeFromThis(&catalog);
  catalog.persisted.locale->SetObjectScope(state.object_scope());
  catalog.persisted.locale->base->SetObjectScope(state.object_scope());
  state.fullscreen_windows.clear();
  bool const desktop_display = shell == CatalogMenuShell::kWindows || shell == CatalogMenuShell::kMac;
  bool const supports_display = desktop_display || display_handling != CatalogDisplayHandling::kNone;
  state.menu = supports_display
      ? CatalogMenuDefinition::ptr{CatalogWindowDisplayMenuDefinition::ptr::Create(ae::CreateWith{domain})}
      : CatalogMenuDefinition::ptr::Create(ae::CreateWith{domain});
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
    std::optional<ae::Domain::ObjectIdSequence> layout_ids;
    if (display_handling != CatalogDisplayHandling::kNone)
      layout_ids.emplace(domain, ae::ObjId{9500}, ae::ObjId{9599});
    state.menu->layout_items = {
      {text("Layout: Automatic", "Раскладка: автоматически"), CatalogMenuAction::kLayoutFlow, static_cast<int>(ui::UiLayoutFlow::kAdaptive)},
      {text("Layout: Horizontal", "Раскладка: горизонтально"), CatalogMenuAction::kLayoutFlow, static_cast<int>(ui::UiLayoutFlow::kHorizontal)},
      {text("Layout: Vertical", "Раскладка: вертикально"), CatalogMenuAction::kLayoutFlow, static_cast<int>(ui::UiLayoutFlow::kVertical)},
      {text("Reset layout to application default", "Вернуть раскладку приложения"), CatalogMenuAction::kResetLayout, 0}};
  }
  if (shell == CatalogMenuShell::kWeb) {
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
  if (supports_display) {
    auto& display = static_cast<CatalogWindowDisplayMenuDefinition&>(*state.menu);
    std::optional<ae::Domain::ObjectIdSequence> display_ids;
    if (!desktop_display)
      display_ids.emplace(domain, ae::ObjId{9600}, ae::ObjId{9699});
    bool const windows = shell == CatalogMenuShell::kWindows;
    bool const mac = shell == CatalogMenuShell::kMac;
    bool const keyboard_hints = windows || (!mac && display_handling == CatalogDisplayHandling::kDesktop);
    display.fullscreen_items = {{
        keyboard_hints ? text("Enter fullscreen (F11)", "На весь экран (F11)")
          : mac ? text("Enter Full Screen", "На весь экран")
                : text("Enter fullscreen", "На весь экран"),
        CatalogMenuAction::kFullscreen, 1}, {
        keyboard_hints ? text("Exit fullscreen (Esc)", "Выйти из полноэкранного режима (Esc)")
          : mac ? text("Exit Full Screen", "Выйти из полноэкранного режима")
                : text("Exit fullscreen", "Выйти из полноэкранного режима"),
        CatalogMenuAction::kFullscreen, 0}};
    ui::UiLayoutNode action{
        .control_id = 5300, .flow = ui::UiLayoutFlow::kLeaf,
        .min_width = 0, .min_height = 28,
        .preferred_width = 0, .preferred_height = 0,
        .max_width = -1, .max_height = -1, .grow = 0,
        .padding = 0, .gap = 0, .alignment = ui::UiLayoutAlignment::kCenter,
        .children = {}};
    display.fullscreen_toolbar = {
        .control_id = 0, .flow = ui::UiLayoutFlow::kVertical,
        .min_width = 0, .min_height = 0,
        .preferred_width = 0, .preferred_height = 0,
        .max_width = -1, .max_height = -1, .grow = 0,
        .padding = 8, .gap = 0, .alignment = ui::UiLayoutAlignment::kEnd,
        .children = {action}};
  }
  if (shell == CatalogMenuShell::kWindows || shell == CatalogMenuShell::kIos) {
    // Keep platform chrome out of the shared menu's ordinary ID sequence.
    ae::Domain::ObjectIdSequence chrome_ids{domain, ae::ObjId{9800}, ae::ObjId{9899}};
    if (shell == CatalogMenuShell::kWindows) {
      state.menu->shell_strings = {text("Loading catalog", "Загрузка каталога")};
    } else {
      state.menu->shell_strings = {text("Settings", "Настройки"), text("Done", "Готово")};
    }
  }
  InitializeRuntimeNode(*state.menu, state);
  PrepareCatalogMenuForRun(catalog);
  state.CaptureBaseState();
  SaveDistilledRoot(state);
}
}  // namespace apptraverse
