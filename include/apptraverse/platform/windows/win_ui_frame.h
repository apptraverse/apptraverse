#ifndef APPTRAVERSE_PLATFORM_WINDOWS_WIN_UI_FRAME_H_
#define APPTRAVERSE_PLATFORM_WINDOWS_WIN_UI_FRAME_H_

#include <windows.h>
#include <string>

namespace apptraverse::ui::windows {

DWORD WindowStyleForClientFrame();
void ClientSizeToOuter(int client_width, int client_height, int& outer_width,
                       int& outer_height, bool has_menu = false);
// Native projection is idempotent even when an unrelated model publication
// visits this presenter. These helpers do not cache model or semantic state.
bool SetNativeWindowTextIfChanged(HWND hwnd, std::wstring const& text);
void SetNativeControlStateIfChanged(HWND hwnd, bool enabled, bool visible);
bool SetNativeChildFrameIfChanged(HWND hwnd, int x, int y, int width, int height);
void ReadModelFrameFromHwnd(HWND hwnd, int& x, int& y, int& client_width,
                            int& client_height);
void ReadChildClientFrame(HWND hwnd, int& x, int& y, int& width, int& height);

}  // namespace apptraverse::ui::windows

#endif
