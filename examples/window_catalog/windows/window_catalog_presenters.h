#ifndef APPTRAVERSE_WINDOWS_WINDOW_CATALOG_PRESENTERS_H_
#define APPTRAVERSE_WINDOWS_WINDOW_CATALOG_PRESENTERS_H_

#include "window_catalog_main_native_frame_wiring.h"
#include "window_catalog_list_presenter_mixin.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/platform/windows/win32_fatal.h"
#include "apptraverse/platform/windows/win_ui_environment.h"

namespace apptraverse::example::surfaces {

// Local machine preference, deliberately separate from portable USER/model Events.
class CatalogDesktopWindowPresenter
    : public WindowCatalogDesktopPresenterMixin<
          ui::windows::WinDesktopWindowPresenter> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::CatalogDesktopWindowPresenter",
      CatalogDesktopWindowPresenter, ui::windows::WinDesktopWindowPresenter, 0)

 protected:
  CatalogDesktopWindowPresenter() = default;

 public:
  explicit CatalogDesktopWindowPresenter(ae::ObjProp prop)
      : WindowCatalogDesktopPresenterMixin{prop} {}

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, catalog);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, catalog);
  }

  bool ShowPresentationContextMenu(int x, int y) override {
    if (window.id() != catalog->persisted.main_window.id()) return false;
    auto const& catalog_window = static_cast<CatalogWindow const&>(*window);
    ui::UiLayoutNode const current = catalog_window.layout->EffectiveRoot();
    HMENU const menu = CreatePopupMenu();
    if (menu == nullptr) apptraverse::FatalWin32("CreatePopupMenu layout", GetLastError());
    AppendMenuW(menu, MF_STRING | (current.flow == ui::UiLayoutFlow::kAdaptive ? MF_CHECKED : 0), 1, L"Layout: Automatic");
    AppendMenuW(menu, MF_STRING | (current.flow == ui::UiLayoutFlow::kHorizontal ? MF_CHECKED : 0), 2, L"Layout: Horizontal");
    AppendMenuW(menu, MF_STRING | (current.flow == ui::UiLayoutFlow::kVertical ? MF_CHECKED : 0), 3, L"Layout: Vertical");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 4, L"Reset layout to application default");
    auto appearance = catalog->persisted.device_state->appearance;
    bool const russian = catalog->persisted.locale->language_index() == 1;
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (appearance.theme == ui::UiThemeMode::kAuto ? MF_CHECKED : 0), 10, russian ? L"Тема: Авто (как в системе)" : L"Theme: Auto (system)");
    AppendMenuW(menu, MF_STRING | (appearance.theme == ui::UiThemeMode::kDark ? MF_CHECKED : 0), 11, russian ? L"Тема: Системная тёмная" : L"Theme: System dark");
    AppendMenuW(menu, MF_STRING | (appearance.theme == ui::UiThemeMode::kLight ? MF_CHECKED : 0), 12, russian ? L"Тема: Системная светлая" : L"Theme: System light");
    AppendMenuW(menu, MF_STRING | (appearance.soften_dark ? MF_CHECKED : 0), 13, russian ? L"Смягчить тёмную тему" : L"Soften dark theme");
    if (x == -1 && y == -1) {
      POINT location{12, 12}; ClientToScreen(hwnd(), &location); x = location.x; y = location.y;
    }
    UINT const action = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, x, y, 0, hwnd(), nullptr);
    DestroyMenu(menu);
    if (action >= 10 && action <= 13) {
      if (action == 10) appearance.theme = ui::UiThemeMode::kAuto;
      if (action == 11) appearance.theme = ui::UiThemeMode::kDark;
      if (action == 12) appearance.theme = ui::UiThemeMode::kLight;
      if (action == 13) appearance.soften_dark = !appearance.soften_dark;
      model_proxy->Invoke(catalog->persisted.device_state.id(), &ui::UiDeviceState::SetAppearance, appearance);
      return true;
    }
    if (action == 4) model_proxy->Invoke(catalog_window.layout.id(), &ui::UiLayoutState::ClearOverride);
    else if (action >= 1 && action <= 3) {
      ui::UiLayoutNode changed = current;
      changed.flow = action == 1 ? ui::UiLayoutFlow::kAdaptive : action == 2 ? ui::UiLayoutFlow::kHorizontal : ui::UiLayoutFlow::kVertical;
      model_proxy->Invoke(catalog_window.layout.id(), &ui::UiLayoutState::SetOverride, std::move(changed));
    }
    return true;
  }


};

class CatalogListContainerPresenter
    : public WindowCatalogListPresenterMixin<
          ui::windows::WinListContainerPresenter> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::CatalogListContainerPresenter",
      CatalogListContainerPresenter, ui::windows::WinListContainerPresenter, 0)

 protected:
  CatalogListContainerPresenter() = default;

 public:
  explicit CatalogListContainerPresenter(ae::ObjProp prop)
      : WindowCatalogListPresenterMixin{prop} {}

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, catalog);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, catalog);
  }

  void SubmitListRowSelectionChangedFromUi(std::size_t row_index) override;
  void SubmitListRowActivatedFromUi(std::size_t row_index) override;
};


}  // namespace apptraverse::example::surfaces

#endif
