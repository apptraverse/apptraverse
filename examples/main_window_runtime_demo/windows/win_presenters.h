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
// HWND. OnLoad registers the native Main class, creates the HWND, and owns
// WndProc. OnUnload destroys the HWND and unregisters the class.
class Win32MainWindowPresenter : public MainWindowPresenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::Win32MainWindowPresenter",
                           Win32MainWindowPresenter, MainWindowPresenter, 0)

 protected:
  Win32MainWindowPresenter() = default;

 public:
  explicit Win32MainWindowPresenter(ae::ObjProp prop)
      : MainWindowPresenter{prop} {}

  AE_OBJECT_REFLECT()

  void OnLoad() override;
  void OnModelChanged() override;
  void OnUnload() override;

  HWND hwnd{nullptr};

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                   LPARAM lparam);
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_MAIN_WINDOW_WIN_PRESENTERS_H_
