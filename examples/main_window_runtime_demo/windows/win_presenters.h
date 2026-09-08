#ifndef APPTRAVERSE_MAIN_WINDOW_WIN_PRESENTERS_H_
#define APPTRAVERSE_MAIN_WINDOW_WIN_PRESENTERS_H_

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif

#include "apptraverse/object_macros.h"

#include <cassert>

#include "main_window_model.h"

namespace apptraverse {

inline wchar_t const kMainWindowClass[] = L"AppTraverseExampleMainWindow";
inline wchar_t const kMainWindowTitle[] = L"Main";

// Most-derived Windows presenter. Construction, Load, and Save do not create
// HWND. HWND is created only in OnLoad during GUI presentation initialization
// and destroyed only in OnUnload.
class Win32MainWindowPresenter : public MainWindowPresenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::Win32MainWindowPresenter",
                           Win32MainWindowPresenter, MainWindowPresenter, 0)

 protected:
  Win32MainWindowPresenter() = default;

 public:
  explicit Win32MainWindowPresenter(ae::ObjProp prop)
      : MainWindowPresenter{prop} {}

  AE_OBJECT_REFLECT()

  void OnLoad() override {
    hwnd = CreateWindowExW(
        0, kMainWindowClass, kMainWindowTitle, WS_OVERLAPPEDWINDOW, window->x,
        window->y, window->width, window->height, nullptr, nullptr,
        GetModuleHandleW(nullptr), presentation_host);
    assert(hwnd != nullptr && "CreateWindowExW Main failed");
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
  }

  void OnUnload() override { DestroyWindow(hwnd); }

  HWND hwnd{nullptr};
};

void EnsureWin32MainWindowPresenterRegistration();

}  // namespace apptraverse

#endif  // APPTRAVERSE_MAIN_WINDOW_WIN_PRESENTERS_H_
