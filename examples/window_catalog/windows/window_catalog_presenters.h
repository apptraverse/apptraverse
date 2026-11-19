#ifndef APPTRAVERSE_WINDOWS_WINDOW_CATALOG_PRESENTERS_H_
#define APPTRAVERSE_WINDOWS_WINDOW_CATALOG_PRESENTERS_H_

#include "window_catalog_main_native_frame_wiring.h"
#include "window_catalog_list_presenter_mixin.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/platform/windows/win32_fatal.h"
#include "apptraverse/platform/windows/win_ui_environment.h"
#include "apptraverse/platform/windows/win_ui_utf8.h"
#include "window_catalog_menu.h"

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

  // Native command IDs are runtime transport tokens. The distilled definition
  // owns labels, choices and behavior; native input passes indices to the model.
  static constexpr UINT kThemeFromSystem = 5100;
  static constexpr UINT kThemeLight = 5101;
  static constexpr UINT kThemeDark = 5102;
  static constexpr UINT kThemeCustomDark = 5103;
  static constexpr UINT kLanguageFirst = 5200;

  CatalogUiState const& UiState() const { return static_cast<CatalogUiState const&>(*LocalDeviceState()); }
  bool HasFullscreenAction() const override { return true; }
  ui::UiLayoutNode const* FullscreenActionLayout() const override {
    return &UiState().DisplayMenu().fullscreen_toolbar;
  }
  std::wstring ResolveFullscreenActionText() const override {
    auto const& item = UiState().DisplayMenu().fullscreen_items[
        UiState().IsWindowFullscreen(window.id()) ? 1 : 0];
    return ui::windows::Utf8ToWide(item.label->Resolve(locale->language_index()));
  }
  void ToggleFullscreenFromUi() override {
    model_proxy->Invoke(UiState().obj_id, &CatalogUiState::ToggleWindowFullscreenFromUi, window.id());
  }
  void ExitFullscreenFromUi() override {
    model_proxy->Invoke(UiState().obj_id, &CatalogUiState::RequestWindowFullscreenFromUi, window.id(), false);
  }
  static UINT CommandId(CatalogMenuItem const& item) {
    return (item.action == CatalogMenuAction::kTheme ? kThemeFromSystem : kLanguageFirst) + item.value;
  }
  HMENU CreatePresentationMenu() override {
    if (window.id() != catalog->persisted.main_window.id()) return nullptr;
    HMENU const menu = CreateMenu();
    if (!menu) apptraverse::FatalWin32("CreateMenu catalog", GetLastError());
    int const language = locale->language_index();
    for (auto const& group : UiState().menu->groups) {
      HMENU const popup = CreatePopupMenu();
      if (!popup || !AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(popup),
          ui::windows::Utf8ToWide(group.label->Resolve(language)).c_str()))
        apptraverse::FatalWin32("AppendMenuW catalog group", GetLastError());
      for (auto const& item : group.items)
        if (!AppendMenuW(popup, MF_STRING, CommandId(item),
            ui::windows::Utf8ToWide(item.label->Resolve(language)).c_str()))
          apptraverse::FatalWin32("AppendMenuW catalog item", GetLastError());
    }
    return menu;
  }
  void OnLoad() override {
    NativeBase::OnLoad();
    SetPresentationFullscreen(UiState().IsWindowFullscreen(window.id()));
    applied_menu_language_ = -1;
    applied_menu_appearance_.reset();
    SyncPresentationMenu();
  }
  void OnModelChanged() override {
    // Exit restores the cached windowed placement before base presentation
    // consumes a topology-driven normal placement revision.
    SetPresentationFullscreen(UiState().IsWindowFullscreen(window.id()));
    NativeBase::OnModelChanged();
    SyncPresentationMenu();
  }
  bool OnCommand(std::uint32_t id, std::uint16_t notification) override {
    if (window.id() != catalog->persisted.main_window.id() || notification != 0) return false;
    auto const& groups = UiState().menu->groups;
    for (unsigned group = 0; group < groups.size(); ++group)
      for (unsigned item = 0; item < groups[group].items.size(); ++item)
        if (id == CommandId(groups[group].items[item])) {
          model_proxy->Invoke(catalog->persisted.device_state.id(), &CatalogUiState::ExecuteMenuCommand, group, item);
          return true;
        }
    return false;
  }

  bool ShowPresentationContextMenu(int x, int y) override {
    if (window.id() != catalog->persisted.main_window.id()) return false;
    auto const& items = UiState().menu->layout_items;
    HMENU const menu = CreatePopupMenu();
    if (menu == nullptr) apptraverse::FatalWin32("CreatePopupMenu layout", GetLastError());
    int const language = locale->language_index();
    for (unsigned index = 0; index < items.size(); ++index) {
      auto const& item = items[index];
      if (item.action == CatalogMenuAction::kResetLayout &&
          !AppendMenuW(menu, MF_SEPARATOR, 0, nullptr))
        apptraverse::FatalWin32("AppendMenuW layout separator", GetLastError());
      if (!AppendMenuW(menu, MF_STRING | (UiState().MenuItemSelected(item) ? MF_CHECKED : 0),
          index + 1, ui::windows::Utf8ToWide(item.label->Resolve(language)).c_str()))
        apptraverse::FatalWin32("AppendMenuW layout item", GetLastError());
    }
    if (x == -1 && y == -1) {
      POINT location{12, 12};
      if (!ClientToScreen(hwnd(), &location))
        apptraverse::FatalWin32("ClientToScreen layout popup", GetLastError());
      x = location.x; y = location.y;
    }
    // RETURNCMD returns zero both for cancellation and failure. LastError is
    // cleared so a dismissed menu does not inherit an unrelated native error.
    SetLastError(ERROR_SUCCESS);
    UINT const action = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, x, y, 0, hwnd(), nullptr);
    DWORD const error = GetLastError();
    if (!DestroyMenu(menu)) apptraverse::FatalWin32("DestroyMenu layout", GetLastError());
    if (action == 0 && error != ERROR_SUCCESS)
      apptraverse::FatalWin32("TrackPopupMenu layout", error);
    if (action != 0)
      model_proxy->Invoke(catalog->persisted.device_state.id(), &CatalogUiState::ExecuteLayoutCommand, action - 1);
    return true;
  }

 private:
  void SyncPresentationMenu() {
    if (window.id() != catalog->persisted.main_window.id()) return;
    int const language = locale->language_index();
    auto const appearance = UiState().appearance;
    if (applied_menu_language_ == language && applied_menu_appearance_ == appearance) return;
    HMENU const menu = GetMenu(hwnd());
    auto const& groups = UiState().menu->groups;
    for (unsigned group_index = 0; group_index < groups.size(); ++group_index) {
      auto const& group = groups[group_index];
      HMENU const popup = GetSubMenu(menu, group_index);
      if (!ModifyMenuW(menu, group_index, MF_BYPOSITION | MF_POPUP, reinterpret_cast<UINT_PTR>(popup),
          ui::windows::Utf8ToWide(group.label->Resolve(language)).c_str()))
        apptraverse::FatalWin32("ModifyMenuW catalog group", GetLastError());
      for (auto const& item : group.items) {
        UINT const id = CommandId(item);
        if (!ModifyMenuW(popup, id, MF_BYCOMMAND | MF_STRING, id,
            ui::windows::Utf8ToWide(item.label->Resolve(language)).c_str()))
          apptraverse::FatalWin32("ModifyMenuW catalog item", GetLastError());
        MENUITEMINFOW info{}; info.cbSize = sizeof(info); info.fMask = MIIM_FTYPE | MIIM_STATE;
        info.fType = MFT_STRING | MFT_RADIOCHECK;
        info.fState = UiState().MenuItemSelected(item) ? MFS_CHECKED : MFS_UNCHECKED;
        if (!SetMenuItemInfoW(popup, id, FALSE, &info))
          apptraverse::FatalWin32("SetMenuItemInfoW catalog item", GetLastError());
      }
    }
    applied_menu_language_ = language;
    applied_menu_appearance_ = appearance;
    if (!DrawMenuBar(hwnd())) apptraverse::FatalWin32("DrawMenuBar catalog", GetLastError());
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
