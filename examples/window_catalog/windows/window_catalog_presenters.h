#ifndef APPTRAVERSE_WINDOWS_WINDOW_CATALOG_PRESENTERS_H_
#define APPTRAVERSE_WINDOWS_WINDOW_CATALOG_PRESENTERS_H_

#include "window_catalog_main_native_frame_wiring.h"
#include "window_catalog_list_presenter_mixin.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/platform/windows/win32_fatal.h"
#include "apptraverse/platform/windows/win_ui_environment.h"
#include <shlobj.h>

namespace apptraverse::example::surfaces {

// Local machine preference, deliberately separate from portable USER/model Events.
inline std::wstring CatalogAppearancePath() {
  wchar_t folder[MAX_PATH]{};
  HRESULT const result = SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, folder);
  if (FAILED(result)) apptraverse::FatalWin32("SHGetFolderPathW appearance", static_cast<DWORD>(result));
  std::wstring directory = std::wstring(folder) + L"\\AppTraverse";
  CreateDirectoryW(directory.c_str(), nullptr);
  directory += L"\\WindowCatalog";
  CreateDirectoryW(directory.c_str(), nullptr);
  return directory + L"\\appearance.ini";
}
inline void LoadCatalogAppearance(std::wstring const& path = CatalogAppearancePath()) {
  auto mode = GetPrivateProfileIntW(L"Appearance", L"Theme", 0, path.c_str());
  if (mode > 2) mode = 0;
  ui::windows::SetWinAppearancePreferences({static_cast<ui::windows::WinThemeMode>(mode),
      GetPrivateProfileIntW(L"Appearance", L"SoftenDark", 0, path.c_str()) != 0});
}
inline void SaveCatalogAppearance(ui::windows::WinAppearancePreferences preferences,
                                  std::wstring const& path = CatalogAppearancePath()) {
  auto const mode = std::to_wstring(static_cast<int>(preferences.theme));
  if (!WritePrivateProfileStringW(L"Appearance", L"Theme", mode.c_str(), path.c_str()) ||
      !WritePrivateProfileStringW(L"Appearance", L"SoftenDark", preferences.soften_dark ? L"1" : L"0", path.c_str()))
    apptraverse::FatalWin32("WritePrivateProfileStringW appearance", GetLastError());
  ui::windows::SetWinAppearancePreferences(preferences);
}

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

  ui::UiLayoutNode const* LayoutRoot() const override {
    // Child Close is supplied by the native caption, so its content-only
    // layout has no presented controls on this backend.
    if (window.id() != catalog->persisted.main_window.id()) return nullptr;
    auto const& catalog_window = static_cast<CatalogWindow const&>(*window);
    return &catalog_window.layout->EffectiveRoot();
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
    auto appearance = ui::windows::GetWinAppearancePreferences();
    bool const russian = catalog->persisted.locale->language_index() == 1;
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (appearance.theme == ui::windows::WinThemeMode::kAuto ? MF_CHECKED : 0), 10, russian ? L"Тема: Авто (как в системе)" : L"Theme: Auto (system)");
    AppendMenuW(menu, MF_STRING | (appearance.theme == ui::windows::WinThemeMode::kDark ? MF_CHECKED : 0), 11, russian ? L"Тема: Системная тёмная" : L"Theme: System dark");
    AppendMenuW(menu, MF_STRING | (appearance.theme == ui::windows::WinThemeMode::kLight ? MF_CHECKED : 0), 12, russian ? L"Тема: Системная светлая" : L"Theme: System light");
    AppendMenuW(menu, MF_STRING | (appearance.soften_dark ? MF_CHECKED : 0), 13, russian ? L"Смягчить тёмную тему" : L"Soften dark theme");
    if (x == -1 && y == -1) {
      POINT location{12, 12}; ClientToScreen(hwnd(), &location); x = location.x; y = location.y;
    }
    UINT const action = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, x, y, 0, hwnd(), nullptr);
    DestroyMenu(menu);
    if (action >= 10 && action <= 13) {
      if (action == 10) appearance.theme = ui::windows::WinThemeMode::kAuto;
      if (action == 11) appearance.theme = ui::windows::WinThemeMode::kDark;
      if (action == 12) appearance.theme = ui::windows::WinThemeMode::kLight;
      if (action == 13) appearance.soften_dark = !appearance.soften_dark;
      SaveCatalogAppearance(appearance);
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

  bool PresentsChildControl(Presenter const& child) const override {
    // Native caption Close supplies this action; identify its content control
    // through the distilled prefab link, independently of model host mode.
    auto const& catalog_window = static_cast<CatalogWindow const&>(*window);
    return child.obj_id != catalog_window.close_button_presenter.id();
  }

  void OnLoad() override {
    assert(SessionMainNativeFrameQueue() != nullptr);
    assert(session_catalog_main_window_id_.is_valid());
    ui::windows::WinDesktopWindowPresenter::OnLoad();
  }

  void ReportNativeFrameToModel(std::int32_t x, std::int32_t y,
                                std::int32_t client_width,
                                std::int32_t client_height,
                                bool updates_restore_geometry) override {
    ::apptraverse::WindowCatalogMainNativeFrameQueue* const queue =
        SessionMainNativeFrameQueue();
    if (this->window.id() == session_catalog_main_window_id_) {
      assert(queue != nullptr);
      assert(session_catalog_obj_id_.is_valid());
      ::apptraverse::PostWindowCatalogMainNativeFrame(
          *queue, this->model_proxy, session_catalog_obj_id_, this->window.id(),
          x, y, client_width, client_height, updates_restore_geometry);
      return;
    }
    this->model_proxy->Invoke(
        catalog.id(), &WindowCatalog::SubmitHolderChildWindowFrameFromUi,
        this->window.id().id(), x, y, client_width, client_height,
        updates_restore_geometry);
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
