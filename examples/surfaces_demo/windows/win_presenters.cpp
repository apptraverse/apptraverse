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

void PostApplicationStop(void* presentation_host) {
  HWND const notify = reinterpret_cast<HWND>(presentation_host);
  if (PostMessageW(notify, WM_APPTRAVERSE_STOP, 0, 0) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("PostMessageW WM_APPTRAVERSE_STOP", err);
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
  return owner->OnCommand(static_cast<std::uint32_t>(LOWORD(wparam)),
                          static_cast<std::uint16_t>(HIWORD(wparam)));
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
    // Native X always requests whole-application stop. Never RemoveSurface.
    PostApplicationStop(presenter->presentation_host);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void Win32SurfacePresenter::OnLoad() {
  wchar_t title[64];
  std::swprintf(title, 64, L"Surface %u", surface->number);
  hwnd = CreateWindowExW(
      0, kSurfacesWindowClass, title, WS_OVERLAPPEDWINDOW, surface->desktop_x,
      surface->desktop_y, surface->desktop_width, surface->desktop_height,
      nullptr, nullptr, GetModuleHandleW(nullptr), this);
  if (hwnd == nullptr) {
    DWORD const err = GetLastError();
    FatalWin32("CreateWindowExW Surface", err);
  }
  SetHwndUserData(hwnd, this);
  add_button = CreateWindowExW(
      0, L"BUTTON", L"Add", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 12, 12, 80,
      28, hwnd,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSurfaceAddButtonId)),
      GetModuleHandleW(nullptr), nullptr);
  if (add_button == nullptr) {
    DWORD const err = GetLastError();
    FatalWin32("CreateWindowExW Add", err);
  }
  SetHwndUserData(add_button, static_cast<Presenter*>(this));
  close_button = CreateWindowExW(
      0, L"BUTTON", L"Close this window", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
      100, 12, 160, 28, hwnd,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSurfaceCloseButtonId)),
      GetModuleHandleW(nullptr), nullptr);
  if (close_button == nullptr) {
    DWORD const err = GetLastError();
    FatalWin32("CreateWindowExW Close this window", err);
  }
  SetHwndUserData(close_button, static_cast<Presenter*>(this));
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
  SetHwndUserData(close_button, nullptr);
  if (DestroyWindow(close_button) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("DestroyWindow Close this window", err);
  }
  close_button = nullptr;
  SetHwndUserData(hwnd, nullptr);
  if (DestroyWindow(hwnd) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("DestroyWindow Surface", err);
  }
  hwnd = nullptr;
}

bool Win32SurfacePresenter::OnCommand(std::uint32_t command_id,
                                      std::uint16_t notification_code) {
  if (notification_code != BN_CLICKED) {
    return false;
  }
  if (command_id == static_cast<std::uint32_t>(kSurfaceAddButtonId)) {
    AddClick();
    return true;
  }
  if (command_id == static_cast<std::uint32_t>(kSurfaceCloseButtonId)) {
    // Real alternative: last Close-button closes the app without Remove.
    if (surface->surfaces->surfaces.size() == 1) {
      PostApplicationStop(presentation_host);
    } else {
      RemoveClick();
    }
    return true;
  }
  return false;
}

void Win32SurfacePresenter::QueueCurrentBounds() {
  RECT rect{};
  if (GetWindowRect(hwnd, &rect) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("GetWindowRect Surface", err);
  }
  UpdateModelBounds(rect.left, rect.top, rect.right - rect.left,
                    rect.bottom - rect.top);
}

}  // namespace apptraverse
