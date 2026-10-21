#ifndef APPTRAVERSE_PLATFORM_WINDOWS_WIN_UI_NATIVE_PUSH_BUTTON_H_
#define APPTRAVERSE_PLATFORM_WINDOWS_WIN_UI_NATIVE_PUSH_BUTTON_H_

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdint>

namespace apptraverse::ui::windows {

// Child BS_PUSHBUTTON at (0,0) size 0; caller sets caption and layout.
HWND CreateChildPushButton(HWND parent_hwnd, std::uint32_t control_id,
                           void* presenter_user_data,
                           char const* create_fatal_operation);

void DestroyChildPushButton(HWND& hwnd, char const* destroy_fatal_operation);

}  // namespace apptraverse::ui::windows

#endif  // APPTRAVERSE_PLATFORM_WINDOWS_WIN_UI_NATIVE_PUSH_BUTTON_H_
