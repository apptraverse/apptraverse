#include "surfaces_demo_win32_child_command.h"

#include "apptraverse/platform/windows/surfaces_win_presenters.h"
#include "apptraverse/platform/windows/win32_fatal.h"
#include "apptraverse/platform/windows/win_ui_push_button_presenter.h"
#include "apptraverse/presenter.h"
#include "apptraverse/surfaces/surfaces_win32_messages.h"

#include "surfaces_demo_close_button.h"
#include "surfaces_demo_surface_window_host.h"

namespace apptraverse::example::surfaces {
namespace {

bool TryHandleDemoLibraryCloseChildCommandImpl(SurfacePresenter* surface_presenter,
                                               WPARAM wparam, LPARAM lparam) {
  if (surface_presenter == nullptr) {
    return false;
  }
  if (surface_presenter->GetClassId() !=
      Win32SurfaceWindowPresenter::kClassId) {
    return false;
  }
  auto* window =
      static_cast<Win32SurfaceWindowPresenter*>(surface_presenter);
  if (window->hwnd == nullptr) {
    return false;
  }

  HWND const child = reinterpret_cast<HWND>(lparam);
  if (child == nullptr) {
    return false;
  }
  if (static_cast<std::uint32_t>(LOWORD(wparam)) !=
      static_cast<std::uint32_t>(kSurfaceCloseButtonId)) {
    return false;
  }
  if (static_cast<std::uint16_t>(HIWORD(wparam)) != BN_CLICKED) {
    return false;
  }
  if (GetParent(child) != window->hwnd) {
    return false;
  }
  if (IsWindowEnabled(child) == FALSE) {
    return true;
  }

  auto* owner =
      reinterpret_cast<Presenter*>(GetWindowLongPtrW(child, GWLP_USERDATA));
  if (owner == nullptr ||
      owner->GetClassId() != ui::windows::WinPushButtonPresenter::kClassId) {
    return false;
  }
  auto close_presenter =
      ui::windows::WinPushButtonPresenter::ptr::MakeFromThis(
          static_cast<ui::windows::WinPushButtonPresenter*>(owner));
  if (!close_presenter->button) {
    return false;
  }
  auto close_button =
      SurfacesDemoCloseButton::ptr{close_presenter->button};
  if (!close_button || !close_button->surface) {
    return false;
  }
  auto host = SurfacesDemoSurfaceWindowHostPresenter::ptr{
      close_presenter->window_presenter};
  if (!host || host->surface_window.operator->() != window) {
    return false;
  }
  if (&*close_button->surface != &*window->surface) {
    return false;
  }
  if (!window->surface || !window->surface->surfaces) {
    return false;
  }
  if (window->surface->surfaces->surfaces.size() != 1) {
    return false;
  }

  HWND const notify =
      reinterpret_cast<HWND>(window->presentation_host);
  if (notify == nullptr) {
    return false;
  }
  if (PostMessageW(notify, WM_APPTRAVERSE_STOP, 0, 0) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("PostMessageW WM_APPTRAVERSE_STOP demo Close", err);
  }
  return true;
}

}  // namespace

bool TryHandleDemoLibraryCloseChildCommand(SurfacePresenter* surface_presenter,
                                           WPARAM wparam, LPARAM lparam) {
  return TryHandleDemoLibraryCloseChildCommandImpl(surface_presenter, wparam,
                                                   lparam);
}

void EnsureSurfacesDemoWin32ChildCommandHook() {
  RegisterSurfaceWin32ChildCommandHook(&TryHandleDemoLibraryCloseChildCommand);
}

}  // namespace apptraverse::example::surfaces
