#include "surfaces_demo_win32_child_command.h"

#include "apptraverse/platform/windows/surfaces_win_presenters.h"
#include "apptraverse/platform/windows/win32_fatal.h"
#include "apptraverse/platform/windows/win_ui_push_button_presenter.h"
#include "apptraverse/presenter.h"
#include "apptraverse/surfaces/surfaces_win32_messages.h"
#include "apptraverse/ui/ui_push_button_presenter.h"

#include "surfaces_demo_close_button.h"
#include "surfaces_demo_surface_window_host.h"

namespace apptraverse::example::surfaces {
namespace {

Presenter* CloseChromePresenter(Surface const& surface) {
  if (!surface.chrome.close_button.is_valid()) {
    return nullptr;
  }
  return surface.chrome.close_button.operator->();
}

}  // namespace

bool TryHandleDemoLibraryCloseChildCommand(
    Win32SurfaceWindowPresenter* window, WPARAM wparam, LPARAM lparam) {
  if (window == nullptr || window->hwnd == nullptr) {
    return false;
  }
  if (!window->surface || !window->surface->surfaces) {
    FatalWin32("Surfaces demo Close child command surface wiring",
               ERROR_INVALID_PARAMETER);
  }

  SurfaceChildBnClickedTarget target{};
  if (!TryParseSurfaceChildBnClicked(window->hwnd, wparam, lparam, &target)) {
    return false;
  }
  if (target.control_id !=
      static_cast<std::uint32_t>(kSurfaceCloseButtonId)) {
    return false;
  }

  Presenter* const close_chrome = CloseChromePresenter(*window->surface);
  if (close_chrome == nullptr) {
    FatalWin32("Surfaces demo Close chrome.close_button wiring",
               ERROR_INVALID_PARAMETER);
  }
  if (target.presenter != close_chrome) {
    return false;
  }
  if (target.presenter->GetClassId() !=
      ui::windows::WinPushButtonPresenter::kClassId) {
    return false;
  }

  auto close_presenter =
      ui::windows::WinPushButtonPresenter::ptr::MakeFromThis(
          static_cast<ui::windows::WinPushButtonPresenter*>(target.presenter));
  if (!close_presenter->button) {
    FatalWin32("Surfaces demo Close WinPushButtonPresenter button wiring",
               ERROR_INVALID_PARAMETER);
  }
  auto close_button = SurfacesDemoCloseButton::ptr{close_presenter->button};
  if (!close_button) {
    FatalWin32("Surfaces demo Close button model type wiring",
               ERROR_INVALID_PARAMETER);
  }
  if (!close_button->surface) {
    FatalWin32("Surfaces demo Close button surface wiring",
               ERROR_INVALID_PARAMETER);
  }
  auto host = SurfacesDemoSurfaceWindowHostPresenter::ptr{
      close_presenter->window_presenter};
  if (!host || host->surface_window.operator->() != window) {
    FatalWin32("Surfaces demo Close host surface_window wiring",
               ERROR_INVALID_PARAMETER);
  }
  if (&*close_button->surface != &*window->surface) {
    return false;
  }

  if (IsWindowEnabled(target.child_hwnd) == FALSE ||
      !close_presenter->button->enabled()) {
    return true;
  }

  if (window->surface->surfaces->surfaces.size() != 1) {
    return false;
  }

  HWND const notify = reinterpret_cast<HWND>(window->presentation_host);
  if (notify == nullptr) {
    FatalWin32("Surfaces demo Close presentation_host wiring",
               ERROR_INVALID_PARAMETER);
  }
  if (PostMessageW(notify, WM_APPTRAVERSE_STOP, 0, 0) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("PostMessageW WM_APPTRAVERSE_STOP demo Close", err);
  }
  return true;
}

}  // namespace apptraverse::example::surfaces
