#include "apptraverse/platform/windows/win_ui_frame.h"

namespace apptraverse::ui::windows {

DWORD WindowStyleForClientFrame() { return WS_OVERLAPPEDWINDOW; }

void ClientSizeToOuter(int client_width, int client_height, int& outer_width,
                       int& outer_height) {
  RECT rect{0, 0, client_width, client_height};
  AdjustWindowRectEx(&rect, WindowStyleForClientFrame(), FALSE, 0);
  outer_width = rect.right - rect.left;
  outer_height = rect.bottom - rect.top;
}

void ReadModelFrameFromHwnd(HWND hwnd, int& x, int& y, int& client_width,
                            int& client_height) {
  RECT frame{};
  RECT client{};
  GetWindowRect(hwnd, &frame);
  GetClientRect(hwnd, &client);
  x = frame.left;
  y = frame.top;
  client_width = client.right - client.left;
  client_height = client.bottom - client.top;
}

}  // namespace apptraverse::ui::windows
