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

#include "main_window_model.h"

namespace apptraverse {

inline wchar_t const kMainWindowClass[] = L"AppTraverseExampleMainWindow";
inline wchar_t const kMainWindowTitle[] = L"Main";

// Most-derived Windows presenter. Construction, Load, and Save do not create
// HWND. HWND is created only in OnLoad during GUI presentation initialization.
class Win32MainWindowPresenter : public MainWindowPresenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::Win32MainWindowPresenter",
                           Win32MainWindowPresenter, MainWindowPresenter, 0)

 protected:
  Win32MainWindowPresenter() = default;

 public:
  explicit Win32MainWindowPresenter(ae::ObjProp prop)
      : MainWindowPresenter{prop} {}

  AE_OBJECT_REFLECT()

  ~Win32MainWindowPresenter() override { DestroyNative(); }

  void OnLoad() override {
    if (hwnd != nullptr || !window) {
      return;
    }
    hwnd = CreateWindowExW(
        0, kMainWindowClass, kMainWindowTitle, WS_OVERLAPPEDWINDOW, window->x,
        window->y, window->width, window->height, nullptr, nullptr,
        GetModuleHandleW(nullptr), presentation_host);
    if (hwnd != nullptr) {
      ShowWindow(hwnd, SW_SHOW);
      UpdateWindow(hwnd);
    }
  }

  void DestroyNative() {
    if (hwnd != nullptr) {
      DestroyWindow(hwnd);
      hwnd = nullptr;
    }
  }

  HWND hwnd{nullptr};
};

void EnsureWin32MainWindowPresenterRegistration();

}  // namespace apptraverse

#endif  // APPTRAVERSE_MAIN_WINDOW_WIN_PRESENTERS_H_
