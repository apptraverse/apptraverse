#include "win_presenters.h"

#include "messenger_win32_messages.h"
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

void RegisterMessengerWin32Classes() {
  HINSTANCE const instance = GetModuleHandleW(nullptr);
  WNDCLASSW wc{};
  wc.lpfnWndProc = &Win32SurfacePresenter::WndProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = kMessengerWindowClass;
  if (RegisterClassW(&wc) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("RegisterClassW Messenger", err);
  }
}

void UnregisterMessengerWin32Classes() {
  HINSTANCE const instance = GetModuleHandleW(nullptr);
  if (UnregisterClassW(kMessengerWindowClass, instance) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("UnregisterClassW Messenger", err);
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
  if (msg == WM_SIZE) {
    RECT client{};
    if (GetClientRect(hwnd, &client) == 0) {
      DWORD const err = GetLastError();
      FatalWin32("GetClientRect Messenger WM_SIZE", err);
    }
    presenter->PresentationSizeChanged(client.right - client.left,
                                       client.bottom - client.top);
    return 0;
  }
  if (msg == WM_CLOSE) {
    PostApplicationStop(presenter->presentation_host);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void Win32SurfacePresenter::OnLoad() {
  hwnd = CreateWindowExW(
      0, kMessengerWindowClass, L"Мессенджер", WS_OVERLAPPEDWINDOW,
      surface->desktop_x, surface->desktop_y, surface->desktop_width,
      surface->desktop_height, nullptr, nullptr, GetModuleHandleW(nullptr),
      this);
  if (hwnd == nullptr) {
    DWORD const err = GetLastError();
    FatalWin32("CreateWindowExW Messenger", err);
  }
  SetHwndUserData(hwnd, this);
  ShowWindow(hwnd, SW_SHOW);
  UpdateWindow(hwnd);
}

void Win32SurfacePresenter::OnModelChanged() {}

void Win32SurfacePresenter::OnUnload() {
  SetHwndUserData(hwnd, nullptr);
  if (DestroyWindow(hwnd) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("DestroyWindow Messenger", err);
  }
  hwnd = nullptr;
}

void Win32SurfacePresenter::QueueCurrentBounds() {
  RECT outer{};
  if (GetWindowRect(hwnd, &outer) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("GetWindowRect Messenger", err);
  }
  UpdateModelBounds(outer.left, outer.top, outer.right - outer.left,
                    outer.bottom - outer.top);
}

}  // namespace apptraverse
