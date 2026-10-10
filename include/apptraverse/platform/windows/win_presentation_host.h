#ifndef APPTRAVERSE_PLATFORM_WINDOWS_WIN_PRESENTATION_HOST_H_
#define APPTRAVERSE_PLATFORM_WINDOWS_WIN_PRESENTATION_HOST_H_

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace apptraverse::ui::windows {

// Passed as Presenter::presentation_host for Win32 UI shells.
// Not serialized; not product-specific.
struct WinPresentationHost {
  HWND notify_hwnd{nullptr};
  void (*on_desktop_window_user_close)(void* opaque){nullptr};
  void* opaque{nullptr};
};

}  // namespace apptraverse::ui::windows

#endif
