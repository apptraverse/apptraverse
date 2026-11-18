#ifndef APPTRAVERSE_WINDOWS_WINDOW_CATALOG_PRESENTERS_H_
#define APPTRAVERSE_WINDOWS_WINDOW_CATALOG_PRESENTERS_H_

#include "window_catalog_main_native_frame_wiring.h"
#include "window_catalog_list_presenter_mixin.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/platform/windows/win32_fatal.h"
#include "apptraverse/platform/windows/win_ui_environment.h"

namespace apptraverse::example::surfaces {

// Preferences are LOCAL model state. Native menu actions queue model Events;
// selection and appearance are applied only after publication to the UI mirror.
class CatalogDesktopWindowPresenter
    : public WindowCatalogDesktopPresenterMixin<
          ui::windows::WinDesktopWindowPresenter> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::CatalogDesktopWindowPresenter",
      CatalogDesktopWindowPresenter, ui::windows::WinDesktopWindowPresenter, 0)

 protected:
  CatalogDesktopWindowPresenter() = default;

 public:
  using NativeBase = WindowCatalogDesktopPresenterMixin<ui::windows::WinDesktopWindowPresenter>;
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

  static constexpr UINT kThemeFromSystem = 5100;
  static constexpr UINT kThemeLight = 5101;
  static constexpr UINT kThemeDark = 5102;
  static constexpr UINT kThemeCustomDark = 5103;

  HMENU CreatePresentationMenu() override {
    if (window.id() != catalog->persisted.main_window.id()) return nullptr;
    HMENU const menu = CreateMenu();
    HMENU const settings = CreatePopupMenu();
    if (!menu || !settings) apptraverse::FatalWin32("CreateMenu settings", GetLastError());
    if (!AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(settings), L"Settings"))
      apptraverse::FatalWin32("AppendMenuW settings", GetLastError());
    for (UINT id = kThemeFromSystem; id <= kThemeCustomDark; ++id)
      if (!AppendMenuW(settings, MF_STRING, id, L""))
        apptraverse::FatalWin32("AppendMenuW theme", GetLastError());
    return menu;
  }

  void OnLoad() override {
    NativeBase::OnLoad();
    applied_menu_language_ = -1;
    applied_menu_appearance_.reset();
    SyncPresentationMenu();
  }

  void OnModelChanged() override {
    NativeBase::OnModelChanged();
    SyncPresentationMenu();
  }

  bool OnCommand(std::uint32_t command_id, std::uint16_t notification_code) override {
    if (window.id() != catalog->persisted.main_window.id() || notification_code != 0 ||
        command_id < kThemeFromSystem || command_id > kThemeCustomDark) return false;
    ui::UiAppearancePreferences appearance;
    appearance.theme = command_id == kThemeFromSystem ? ui::UiThemeMode::kAuto :
        command_id == kThemeLight ? ui::UiThemeMode::kLight : ui::UiThemeMode::kDark;
    appearance.soften_dark = command_id == kThemeCustomDark;
    model_proxy->Invoke(catalog->persisted.device_state.id(), &ui::UiDeviceState::SetAppearance, appearance);
    return true;
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
    if (x == -1 && y == -1) {
      POINT location{12, 12}; ClientToScreen(hwnd(), &location); x = location.x; y = location.y;
    }
    UINT const action = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, x, y, 0, hwnd(), nullptr);
    DestroyMenu(menu);
    if (action == 4) model_proxy->Invoke(catalog_window.layout.id(), &ui::UiLayoutState::ClearOverride);
    else if (action >= 1 && action <= 3) {
      ui::UiLayoutNode changed = current;
      changed.flow = action == 1 ? ui::UiLayoutFlow::kAdaptive : action == 2 ? ui::UiLayoutFlow::kHorizontal : ui::UiLayoutFlow::kVertical;
      model_proxy->Invoke(catalog_window.layout.id(), &ui::UiLayoutState::SetOverride, std::move(changed));
    }
    return true;
  }

 private:
  void SyncPresentationMenu() {
    if (window.id() != catalog->persisted.main_window.id()) return;
    HMENU const menu = GetMenu(hwnd());
    HMENU const settings = GetSubMenu(menu, 0);
    int const language = catalog->persisted.locale->language_index();
    auto const appearance = catalog->persisted.device_state->appearance;
    if (applied_menu_language_ != language) {
      bool const russian = language == 1;
      if (!ModifyMenuW(menu, 0, MF_BYPOSITION | MF_POPUP, reinterpret_cast<UINT_PTR>(settings),
                  russian ? L"Настройки" : L"Settings"))
        apptraverse::FatalWin32("ModifyMenuW settings", GetLastError());
      wchar_t const* labels[] = {russian ? L"Как в системе" : L"From system",
          russian ? L"Светлая" : L"Light", russian ? L"Тёмная" : L"Dark",
          russian ? L"Своя тёмная" : L"Custom dark"};
      for (UINT id = kThemeFromSystem; id <= kThemeCustomDark; ++id)
        if (!ModifyMenuW(settings, id, MF_BYCOMMAND | MF_STRING, id, labels[id - kThemeFromSystem]))
          apptraverse::FatalWin32("ModifyMenuW theme", GetLastError());
      applied_menu_language_ = language;
      applied_menu_appearance_.reset();
      if (!DrawMenuBar(hwnd())) apptraverse::FatalWin32("DrawMenuBar settings", GetLastError());
    }
    if (applied_menu_appearance_ != appearance) {
      UINT const selected = appearance.theme == ui::UiThemeMode::kAuto ? kThemeFromSystem :
          appearance.theme == ui::UiThemeMode::kLight ? kThemeLight :
          appearance.soften_dark ? kThemeCustomDark : kThemeDark;
      if (!CheckMenuRadioItem(settings, kThemeFromSystem, kThemeCustomDark, selected, MF_BYCOMMAND))
        apptraverse::FatalWin32("CheckMenuRadioItem theme", GetLastError());
      applied_menu_appearance_ = appearance;
    }
  }
  int applied_menu_language_{-1};
  std::optional<ui::UiAppearancePreferences> applied_menu_appearance_;
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
