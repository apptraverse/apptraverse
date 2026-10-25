#include "apptraverse/platform/windows/win_application_loading_window.h"

#include "apptraverse/platform/windows/win32_fatal.h"

namespace apptraverse {
namespace windows {

WinApplicationLoadingWindow::WinApplicationLoadingWindow(
    wchar_t const* class_name, wchar_t const* title, wchar_t const* message,
    int x, int y, int width, int height)
    : class_name_{class_name}, paint_text_{message} {
  instance_ = GetModuleHandleW(nullptr);

  WNDCLASSW wc{};
  wc.lpfnWndProc = &WinApplicationLoadingWindow::WndProc;
  wc.hInstance = instance_;
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = class_name_.c_str();
  if (RegisterClassW(&wc) == 0) {
    FatalWin32("RegisterClassW WinApplicationLoadingWindow", GetLastError());
  }

  hwnd_ = CreateWindowExW(0, class_name_.c_str(), title,
                            WS_OVERLAPPED | WS_CAPTION | WS_VISIBLE, x, y,
                            width, height, nullptr, nullptr, instance_, this);
  if (hwnd_ == nullptr) {
    FatalWin32("CreateWindowExW WinApplicationLoadingWindow", GetLastError());
  }
}

WinApplicationLoadingWindow::~WinApplicationLoadingWindow() {
  Close();
  if (instance_ != nullptr) {
    if (UnregisterClassW(class_name_.c_str(), instance_) == 0) {
      FatalWin32("UnregisterClassW WinApplicationLoadingWindow", GetLastError());
    }
  }
}

void WinApplicationLoadingWindow::Close() {
  if (hwnd_ == nullptr) {
    return;
  }
  if (DestroyWindow(hwnd_) == 0) {
    FatalWin32("DestroyWindow WinApplicationLoadingWindow", GetLastError());
  }
  hwnd_ = nullptr;
}

bool WinApplicationLoadingWindow::is_open() const { return hwnd_ != nullptr; }

HWND WinApplicationLoadingWindow::hwnd() const { return hwnd_; }

LRESULT CALLBACK WinApplicationLoadingWindow::WndProc(HWND hwnd, UINT msg,
                                                      WPARAM wparam,
                                                      LPARAM lparam) {
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
  auto* self = reinterpret_cast<WinApplicationLoadingWindow*>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (self == nullptr) {
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
  return self->Handle(hwnd, msg, wparam, lparam);
}

LRESULT WinApplicationLoadingWindow::Handle(HWND hwnd, UINT msg, WPARAM wparam,
                                            LPARAM lparam) {
  if (msg == WM_PAINT) {
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT client{};
    GetClientRect(hwnd, &client);
    DrawTextW(hdc, paint_text_.c_str(), -1, &client,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    EndPaint(hwnd, &ps);
    return 0;
  }
  if (msg == WM_CLOSE) {
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace windows
}  // namespace apptraverse
