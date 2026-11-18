#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif
#include "chat_windows_presenter.h"

#include "apptraverse/model_object_proxy.h"
#include "apptraverse/platform/windows/win32_fatal.h"
#include "apptraverse/platform/windows/win_ui_utf8.h"
#include "chat_ui_model.h"

namespace apptraverse::chat {

// Same LOCAL appearance Events and OS baseline as WindowCatalog. Labels are
// distilled chat model strings, and input is sent to the model through proxy.
HMENU ChatDesktopWindowPresenter::CreatePresentationMenu() {
  auto const& chat = static_cast<gui::ChatUi const&>(*window);
  HMENU const menu = CreateMenu();
  HMENU const theme = CreatePopupMenu();
  if (!menu || !theme) FatalWin32("CreateMenu chat", GetLastError());
  if (!AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(theme),
                   ui::windows::Utf8ToWide(chat.Text(gui::Label::Theme)).c_str()))
    FatalWin32("AppendMenuW chat theme", GetLastError());
  for (unsigned i = 0; i < 4; ++i)
    if (!AppendMenuW(theme, MF_STRING, kThemeFirst + i,
                     ui::windows::Utf8ToWide(chat.Text(static_cast<gui::Label>(
                         static_cast<unsigned>(gui::Label::ThemeSystem) + i))).c_str()))
      FatalWin32("AppendMenuW chat theme item", GetLastError());
  return menu;
}

void ChatDesktopWindowPresenter::OnLoad() {
  WinDesktopWindowPresenter::OnLoad();
  SyncThemeMenu();
}

void ChatDesktopWindowPresenter::OnModelChanged() {
  WinDesktopWindowPresenter::OnModelChanged();
  SyncThemeMenu();
}

bool ChatDesktopWindowPresenter::OnCommand(std::uint32_t id,
                                           std::uint16_t notification) {
  if (notification != 0 || id < kThemeFirst || id >= kThemeFirst + 4)
    return false;
  model_proxy->Invoke(window.id(), &gui::ChatUi::SelectTheme, id - kThemeFirst);
  return true;
}

void ChatDesktopWindowPresenter::SyncThemeMenu() {
  auto const appearance =
      static_cast<gui::ChatUi const&>(*window).device_state->appearance;
  if (applied_appearance_ == appearance) return;
  HMENU const popup = GetSubMenu(GetMenu(hwnd()), 0);
  UINT const selected = appearance.theme == ui::UiThemeMode::kAuto ? 0 :
      appearance.theme == ui::UiThemeMode::kLight ? 1 :
      appearance.soften_dark ? 3 : 2;
  for (UINT i = 0; i < 4; ++i) {
    MENUITEMINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = MIIM_FTYPE | MIIM_STATE;
    info.fType = MFT_STRING | MFT_RADIOCHECK;
    info.fState = i == selected ? MFS_CHECKED : MFS_UNCHECKED;
    if (!SetMenuItemInfoW(popup, kThemeFirst + i, FALSE, &info))
      FatalWin32("SetMenuItemInfoW chat theme", GetLastError());
  }
  applied_appearance_ = appearance;
  if (!DrawMenuBar(hwnd())) FatalWin32("DrawMenuBar chat", GetLastError());
}

APPTRAVERSE_REGISTER(ChatDesktopWindowPresenter);
void EnsureChatWindowsPresenterRegistration() {
  (void)&g_apptraverse_registrar_ChatDesktopWindowPresenter;
}

}  // namespace apptraverse::chat
