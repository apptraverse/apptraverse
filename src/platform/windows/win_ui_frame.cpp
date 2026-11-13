#include "apptraverse/platform/windows/win_ui_frame.h"
#include "apptraverse/platform/windows/win32_fatal.h"

namespace apptraverse::ui::windows {

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
                       int& outer_height) {
  RECT rect{0, 0, client_width, client_height};
  if (!AdjustWindowRectEx(&rect, WindowStyleForClientFrame(), FALSE, 0)) {
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
