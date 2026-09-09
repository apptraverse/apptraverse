#ifndef APPTRAVERSE_MAIN_WINDOW_WIN32_MESSAGES_H_
#define APPTRAVERSE_MAIN_WINDOW_WIN32_MESSAGES_H_

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace apptraverse {

// Notify HWND messages. Shared by WinApp and Win32MainWindowPresenter so the
// presenter does not depend on WinApp.
inline constexpr UINT WM_APPTRAVERSE_PUBLISHED = WM_APP + 1;
inline constexpr UINT WM_APPTRAVERSE_STOP = WM_APP + 2;

}  // namespace apptraverse

#endif  // APPTRAVERSE_MAIN_WINDOW_WIN32_MESSAGES_H_
