#include "apptraverse/platform/windows/win_ui_frame.h"
#include "apptraverse/platform/windows/win32_fatal.h"

namespace apptraverse::ui::windows {

bool SetNativeWindowTextIfChanged(HWND hwnd, std::wstring const& text) {
  SetLastError(ERROR_SUCCESS);
  int const length = GetWindowTextLengthW(hwnd);
  if (length == 0 && GetLastError() != ERROR_SUCCESS)
    FatalWin32("GetWindowTextLengthW native projection", GetLastError());
  std::wstring current(static_cast<std::size_t>(length) + 1, L'\0');
  int const copied = GetWindowTextW(hwnd, current.data(), length + 1);
  if (copied == 0 && length != 0)
    FatalWin32("GetWindowTextW native projection", GetLastError());
  current.resize(copied);
  if (current == text) return false;
  if (!SetWindowTextW(hwnd, text.c_str()))
    FatalWin32("SetWindowTextW native projection", GetLastError());
  return true;
}

void SetNativeControlStateIfChanged(HWND hwnd, bool enabled, bool visible) {
  if ((IsWindowEnabled(hwnd) != FALSE) != enabled) EnableWindow(hwnd, enabled);
  bool const currently_visible = (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_VISIBLE) != 0;
  if (currently_visible != visible) ShowWindow(hwnd, visible ? SW_SHOW : SW_HIDE);
}

bool SetNativeChildFrameIfChanged(HWND hwnd, int x, int y, int width, int height) {
  int native_x, native_y, native_width, native_height;
  ReadChildClientFrame(hwnd, native_x, native_y, native_width, native_height);
  if (native_x == x && native_y == y && native_width == width && native_height == height)
    return false;
  if (!SetWindowPos(hwnd, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE))
    FatalWin32("SetWindowPos native child projection", GetLastError());
  return true;
}

void ReadChildClientFrame(HWND hwnd, int& x, int& y, int& width, int& height) {
  RECT rect{};
  if (GetWindowRect(hwnd, &rect) == 0) {
    FatalWin32("GetWindowRect child layout", GetLastError());
  }
  width = rect.right - rect.left;
  height = rect.bottom - rect.top;
  POINT origin{rect.left, rect.top};
  SetLastError(0);
  if (MapWindowPoints(HWND_DESKTOP, GetParent(hwnd), &origin, 1) == 0 &&
      GetLastError() != ERROR_SUCCESS) {
    FatalWin32("MapWindowPoints child layout", GetLastError());
  }
  x = origin.x;
  y = origin.y;
}

DWORD WindowStyleForClientFrame() { return WS_OVERLAPPEDWINDOW; }

void ClientSizeToOuter(int client_width, int client_height, int& outer_width,
                       int& outer_height, bool has_menu) {
  RECT rect{0, 0, client_width, client_height};
  if (!AdjustWindowRectEx(&rect, WindowStyleForClientFrame(), has_menu, 0)) {
    FatalWin32("AdjustWindowRectEx client layout", GetLastError());
  }
  outer_width = rect.right - rect.left;
  outer_height = rect.bottom - rect.top;
}

void ReadModelFrameFromHwnd(HWND hwnd, int& x, int& y, int& client_width,
                            int& client_height) {
  RECT frame{};
  RECT client{};
  if (!GetWindowRect(hwnd, &frame)) {
    FatalWin32("GetWindowRect model frame", GetLastError());
  }
  if (!GetClientRect(hwnd, &client)) {
    FatalWin32("GetClientRect model frame", GetLastError());
  }
  x = frame.left;
  y = frame.top;
  client_width = client.right - client.left;
  client_height = client.bottom - client.top;
}

}  // namespace apptraverse::ui::windows
