#include "win_presenters.h"

#include "main_window_lifecycle.h"
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
  if (msg == WM_WINDOWPOSCHANGED && presenter->hwnd != nullptr) {
    RECT rect{};
    if (GetWindowRect(hwnd, &rect) == 0) {
      DWORD const err = GetLastError();
      FatalWin32("GetWindowRect WM_WINDOWPOSCHANGED", err);
    }
    WindowChangedCommand command;
    command.x = static_cast<std::int32_t>(rect.left);
    command.y = static_cast<std::int32_t>(rect.top);
    command.width = static_cast<std::int32_t>(rect.right - rect.left);
    command.height = static_cast<std::int32_t>(rect.bottom - rect.top);
    HWND const notify = reinterpret_cast<HWND>(presenter->presentation_host);
    // Same GUI thread: SendMessageW invokes notify WndProc before return, and
    // WinApp copies the four integers before this stack command is used again.
    SendMessageW(notify, WM_APPTRAVERSE_WINDOW_CHANGED, 0,
                 reinterpret_cast<LPARAM>(&command));
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void Win32MainWindowPresenter::OnModelChanged() {
  RECT actual{};
  if (GetWindowRect(hwnd, &actual) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("GetWindowRect OnModelChanged", err);
  }
  auto const desired_x = window->x;
  auto const desired_y = window->y;
  auto const desired_w = window->width;
  auto const desired_h = window->height;
  if (actual.left == desired_x && actual.top == desired_y &&
      actual.right - actual.left == desired_w &&
      actual.bottom - actual.top == desired_h) {
    return;
  }
  if (SetWindowPos(hwnd, nullptr, desired_x, desired_y, desired_w, desired_h,
                   SWP_NOZORDER | SWP_NOACTIVATE) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("SetWindowPos Main", err);
  }
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
  HWND const dying = hwnd;
  // DestroyWindow sends WM_WINDOWPOSCHANGED. Clear hwnd first so that
  // teardown notification is not treated as a live geometry command.
  hwnd = nullptr;
  if (DestroyWindow(dying) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("DestroyWindow Main", err);
  }
  if (UnregisterClassW(kMainWindowClass, GetModuleHandleW(nullptr)) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("UnregisterClassW Main", err);
  }
}

}  // namespace apptraverse
