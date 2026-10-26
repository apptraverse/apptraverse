#include "apptraverse/platform/windows/win_ui_push_button_presenter.h"

#include "apptraverse/platform/windows/win_ui_native_push_button.h"
#include "apptraverse/platform/windows/win_ui_utf8.h"
#include "apptraverse/platform/windows/win32_fatal.h"
#include "apptraverse/ui/ui_localization_string.h"

namespace apptraverse::ui::windows {

#if defined(APPTRAVERSE_WIN_DESKTOP_FRAME_SYNC_TEST)
int WinPushButtonPresenter::test_set_window_pos_count_{0};

int WinPushButtonPresenter::TestSetWindowPosCount() {
  return test_set_window_pos_count_;
}

void WinPushButtonPresenter::TestResetSetWindowPosCount() {
  test_set_window_pos_count_ = 0;
}
#endif

namespace {

bool ReadChildClientFrame(HWND hwnd, int& x, int& y, int& width, int& height) {
  RECT rect{};
  if (GetWindowRect(hwnd, &rect) == 0) {
    return false;
  }
  width = rect.right - rect.left;
  height = rect.bottom - rect.top;
  HWND const parent = GetParent(hwnd);
  if (parent == nullptr) {
    return false;
  }
  POINT origin{rect.left, rect.top};
  SetLastError(0);
  if (MapWindowPoints(HWND_DESKTOP, parent, &origin, 1) == 0) {
    DWORD const err = GetLastError();
    if (err != ERROR_SUCCESS) {
      return false;
    }
  }
  x = origin.x;
  y = origin.y;
  return true;
}

ui::UiControlLayout EffectiveLayout(WinPushButtonPresenter const& presenter) {
  ui::UiControlLayout layout{
      presenter.layout_x, presenter.layout_y, presenter.layout_width,
      presenter.layout_height, presenter.control_id};
  if (presenter.window_presenter) {
    return presenter.window_presenter->ResolveChildLayout(layout);
  }
  return layout;
}

}  // namespace

void WinPushButtonPresenter::OnLoad() {
  if (!window_presenter) {
    FatalWin32("WinPushButtonPresenter OnLoad window_presenter",
               ERROR_INVALID_PARAMETER);
  }
  auto* const parent_hwnd = reinterpret_cast<HWND>(
      window_presenter->NativeChildParentHandle());
  if (parent_hwnd == nullptr) {
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
  ui::UiControlLayout const layout = EffectiveLayout(*this);
  hwnd_ = CreateChildPushButton({
      .parent_hwnd = parent_hwnd,
      .control_id = control_id,
      .initial_caption = caption.c_str(),
      .x = layout.x,
      .y = layout.y,
      .width = layout.width,
      .height = layout.height,
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
  ApplyLayout();
}

void WinPushButtonPresenter::ApplyLayout() {
  if (hwnd_ == nullptr) {
    return;
  }
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  if (!ReadChildClientFrame(hwnd_, x, y, width, height)) {
    DWORD const err = GetLastError();
    FatalWin32("GetWindowRect Ui push button layout", err);
  }
  ui::UiControlLayout const layout = EffectiveLayout(*this);
  if (x == layout.x && y == layout.y && width == layout.width &&
      height == layout.height) {
    return;
  }
#if defined(APPTRAVERSE_WIN_DESKTOP_FRAME_SYNC_TEST)
  ++test_set_window_pos_count_;
#endif
  if (SetWindowPos(hwnd_, nullptr, layout.x, layout.y, layout.width,
                   layout.height, SWP_NOZORDER | SWP_NOACTIVATE) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("SetWindowPos Ui push button", err);
  }
}

void WinPushButtonPresenter::OnModelChanged() { SyncFromMirror(); }

void WinPushButtonPresenter::OnUnload() {
  DestroyChildPushButton(hwnd_, "DestroyWindow Ui push button");
}

}  // namespace apptraverse::ui::windows
