#ifndef APPTRAVERSE_MAIN_WINDOW_WIN32_FATAL_H_
#define APPTRAVERSE_MAIN_WINDOW_WIN32_FATAL_H_

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace apptraverse {

// Shared Win32 fatal path for this example. Capture GetLastError at the
// failing call, then pass that code here. Does not return.
[[noreturn]] void FatalWin32(char const* operation, DWORD error);

}  // namespace apptraverse

#endif  // APPTRAVERSE_MAIN_WINDOW_WIN32_FATAL_H_
