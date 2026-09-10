#include "win_presenters.h"

#include <cstdio>

#include "surfaces_win32_messages.h"
#include "win32_fatal.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(Win32SurfacePresenter);

void SetHwndUserData(HWND hwnd, void* value) {
  SetLastError(0);
  LONG_PTR const previous =
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(value));
  if (previous == 0 && GetLastError() != 0) {
    DWORD const err = GetLastError();
    FatalWin32("SetWindowLongPtrW GWLP_USERDATA", err);
  }
}

}  // namespace

void EnsureWin32SurfacePresenterRegistration() {
  EnsureDesktopSurfacePresenterRegistration();
  (void)&g_apptraverse_registrar_Win32SurfacePresenter;
}

bool DispatchChildCommand(WPARAM wparam, LPARAM lparam) {
  HWND const child = reinterpret_cast<HWND>(lparam);
  if (child == nullptr) {
    return false;
  }
  auto* owner =
      reinterpret_cast<Presenter*>(GetWindowLongPtrW(child, GWLP_USERDATA));
  if (owner == nullptr) {
    return false;
  }
  return owner->OnCommand(static_cast<std::uint16_t>(HIWORD(wparam)));
}

void RegisterSurfacesWin32Classes() {
  HINSTANCE const instance = GetModuleHandleW(nullptr);
  WNDCLASSW wc{};
  wc.lpfnWndProc = &Win32SurfacePresenter::WndProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = kSurfacesWindowClass;
  if (RegisterClassW(&wc) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("RegisterClassW Surfaces", err);
  }
}

void UnregisterSurfacesWin32Classes() {
  HINSTANCE const instance = GetModuleHandleW(nullptr);
  if (UnregisterClassW(kSurfacesWindowClass, instance) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("UnregisterClassW Surfaces", err);
  }
}

LRESULT CALLBACK Win32SurfacePresenter::WndProc(HWND hwnd, UINT msg,
                                                WPARAM wparam, LPARAM lparam) {
  if (msg == WM_NCCREATE) {
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
  auto* presenter = reinterpret_cast<Win32SurfacePresenter*>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (presenter == nullptr) {
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
  if (msg == WM_COMMAND && DispatchChildCommand(wparam, lparam)) {
    return 0;
  }
  if (msg == WM_CLOSE) {
    Surfaces& parent = *presenter->surface->surfaces;
    // Real alternative: last live Surface window exits the app without Remove.
    if (parent.surfaces.size() == 1) {
      HWND const notify = reinterpret_cast<HWND>(presenter->presentation_host);
      if (PostMessageW(notify, WM_APPTRAVERSE_STOP, 0, 0) == 0) {
        DWORD const err = GetLastError();
        FatalWin32("PostMessageW WM_APPTRAVERSE_STOP", err);
      }
      return 0;
    }
    presenter->RemoveClick();
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void Win32SurfacePresenter::OnLoad() {
  wchar_t title[64];
  std::swprintf(title, 64, L"Surface %u", surface->number);
  hwnd = CreateWindowExW(0, kSurfacesWindowClass, title, WS_OVERLAPPEDWINDOW,
                         CW_USEDEFAULT, CW_USEDEFAULT, 360, 240, nullptr,
                         nullptr, GetModuleHandleW(nullptr), this);
  if (hwnd == nullptr) {
    DWORD const err = GetLastError();
    FatalWin32("CreateWindowExW Surface", err);
  }
  SetHwndUserData(hwnd, this);
  add_button = CreateWindowExW(
      0, L"BUTTON", L"Add", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 12, 12, 100,
      28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSurfaceAddButtonId)),
      GetModuleHandleW(nullptr), nullptr);
  if (add_button == nullptr) {
    DWORD const err = GetLastError();
    FatalWin32("CreateWindowExW Add", err);
  }
  SetHwndUserData(add_button, static_cast<Presenter*>(this));
  ShowWindow(hwnd, SW_SHOW);
  UpdateWindow(hwnd);
}

void Win32SurfacePresenter::OnModelChanged() {}

void Win32SurfacePresenter::OnUnload() {
  SetHwndUserData(add_button, nullptr);
  if (DestroyWindow(add_button) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("DestroyWindow Add", err);
  }
  add_button = nullptr;
  SetHwndUserData(hwnd, nullptr);
  if (DestroyWindow(hwnd) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("DestroyWindow Surface", err);
  }
  hwnd = nullptr;
}

bool Win32SurfacePresenter::OnCommand(std::uint16_t notification_code) {
  if (notification_code != BN_CLICKED) {
    return false;
  }
  AddClick();
  return true;
}

}  // namespace apptraverse
