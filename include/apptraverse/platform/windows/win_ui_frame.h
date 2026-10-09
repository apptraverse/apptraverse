#ifndef APPTRAVERSE_PLATFORM_WINDOWS_WIN_UI_FRAME_H_
#define APPTRAVERSE_PLATFORM_WINDOWS_WIN_UI_FRAME_H_

#include <windows.h>

namespace apptraverse::ui::windows {

DWORD WindowStyleForClientFrame();
void ClientSizeToOuter(int client_width, int client_height, int& outer_width,
                       int& outer_height);
void ReadModelFrameFromHwnd(HWND hwnd, int& x, int& y, int& client_width,
                            int& client_height);

}  // namespace apptraverse::ui::windows

#endif
