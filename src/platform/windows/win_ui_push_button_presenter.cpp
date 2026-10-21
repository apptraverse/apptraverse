#include "apptraverse/platform/windows/win_ui_push_button_presenter.h"

#include "apptraverse/platform/windows/win_ui_native_push_button.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/platform/windows/win_ui_utf8.h"
#include "apptraverse/platform/windows/win32_fatal.h"
#include "apptraverse/ui/ui_localization_string.h"

namespace apptraverse::ui::windows {

WinDesktopWindowPresenter* WinPushButtonPresenter::Desktop() const {
  if (!window_presenter) {
    return nullptr;
  }
  return static_cast<WinDesktopWindowPresenter*>(window_presenter.operator->());
}

void WinPushButtonPresenter::OnLoad() {
  auto* desktop = Desktop();
  if (desktop == nullptr || desktop->hwnd() == nullptr) {
    FatalWin32("WinPushButtonPresenter OnLoad parent", ERROR_INVALID_PARAMETER);
  }
  if (!button || control_id == 0) {
    FatalWin32("WinPushButtonPresenter OnLoad button", ERROR_INVALID_PARAMETER);
  }
  std::wstring caption;
  if (button->label && window_presenter && window_presenter->locale) {
    caption = Utf8ToWide(
        button->label->Resolve(window_presenter->locale->language_index()));
  }
  hwnd_ = CreateChildPushButton({
      .parent_hwnd = desktop->hwnd(),
      .control_id = control_id,
      .initial_caption = caption.c_str(),
      .x = layout_x,
      .y = layout_y,
      .width = layout_width,
      .height = layout_height,
      .presenter_user_data = this,
      .create_fatal_operation = "CreateWindowExW Ui push button",
  });
  SyncFromMirror();
}

void WinPushButtonPresenter::SyncFromMirror() {
  if (hwnd_ == nullptr || !button) {
    return;
  }
  if (button->label && window_presenter && window_presenter->locale) {
    SetWindowTextW(
        hwnd_, Utf8ToWide(button->label->Resolve(
                          window_presenter->locale->language_index()))
                   .c_str());
  }
  EnableWindow(hwnd_, button->enabled() ? TRUE : FALSE);
}

void WinPushButtonPresenter::OnModelChanged() { SyncFromMirror(); }

void WinPushButtonPresenter::OnUnload() {
  DestroyChildPushButton(hwnd_, "DestroyWindow Ui push button");
}

}  // namespace apptraverse::ui::windows
