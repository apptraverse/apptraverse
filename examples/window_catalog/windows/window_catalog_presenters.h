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
  bool PresentsChildControl(Presenter const& child) const override {
    // Windows uses the application menu for language. The common action stays
    // available to supplies whose platform presents it in content.
    return child.obj_id != catalog->persisted.presenters.language_button_presenter.id() && NativeBase::PresentsChildControl(child);
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
    applied_menu_language_ = -1;
    applied_menu_appearance_.reset();
    SyncPresentationMenu();
  }
  void OnModelChanged() override {
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
