#include "win_presenters.h"

#include "main_window_win32_messages.h"
#include "win32_fatal.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(Win32MainWindowPresenter);

}  // namespace

LRESULT CALLBACK Win32MainWindowPresenter::WndProc(HWND hwnd, UINT msg,
                                                    WPARAM wparam,
                                                    LPARAM lparam) {
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
  auto* presenter = reinterpret_cast<Win32MainWindowPresenter*>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (presenter == nullptr) {
    // WM_GETMINMAXINFO is sent before WM_NCCREATE for overlapped windows.
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
  if (msg == WM_CLOSE) {
    HWND const notify = reinterpret_cast<HWND>(presenter->presentation_host);
    if (PostMessageW(notify, WM_APPTRAVERSE_STOP, 0, 0) == 0) {
      DWORD const err = GetLastError();
      FatalWin32("PostMessageW WM_APPTRAVERSE_STOP", err);
    }
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void Win32MainWindowPresenter::OnLoad() {
  WNDCLASSW wc{};
  wc.lpfnWndProc = &Win32MainWindowPresenter::WndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = kMainWindowClass;
  if (RegisterClassW(&wc) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("RegisterClassW Main", err);
  }
  hwnd = CreateWindowExW(
      0, kMainWindowClass, kMainWindowTitle, WS_OVERLAPPEDWINDOW, window->x,
      window->y, window->width, window->height, nullptr, nullptr,
      GetModuleHandleW(nullptr), this);
  if (hwnd == nullptr) {
    DWORD const err = GetLastError();
    FatalWin32("CreateWindowExW Main", err);
  }
  ShowWindow(hwnd, SW_SHOW);
  UpdateWindow(hwnd);
}

void Win32MainWindowPresenter::OnUnload() {
  if (DestroyWindow(hwnd) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("DestroyWindow Main", err);
  }
  if (UnregisterClassW(kMainWindowClass, GetModuleHandleW(nullptr)) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("UnregisterClassW Main", err);
  }
}

}  // namespace apptraverse
