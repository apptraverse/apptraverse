#ifndef APPTRAVERSE_MAIN_WINDOW_WIN_PRESENTERS_H_
#define APPTRAVERSE_MAIN_WINDOW_WIN_PRESENTERS_H_

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif

#include "main_window_model.h"

namespace apptraverse {

inline wchar_t const kMainWindowClass[] = L"AppTraverseExampleMainWindow";
inline wchar_t const kMainWindowTitle[] = L"Main";

class MainWindowPresenter {
 public:
  HWND hwnd{nullptr};

  void Create(MainWindow const& window, void* owner) {
    hwnd = CreateWindowExW(
        0, kMainWindowClass, kMainWindowTitle, WS_OVERLAPPEDWINDOW,
        window.x, window.y, window.width, window.height, nullptr, nullptr,
        GetModuleHandleW(nullptr), owner);
  }

  void Destroy() {
    if (hwnd != nullptr) {
      DestroyWindow(hwnd);
      hwnd = nullptr;
    }
  }
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_MAIN_WINDOW_WIN_PRESENTERS_H_
