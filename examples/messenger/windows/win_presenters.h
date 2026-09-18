#ifndef APPTRAVERSE_MESSENGER_WIN_PRESENTERS_H_
#define APPTRAVERSE_MESSENGER_WIN_PRESENTERS_H_

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif

#include "apptraverse/object_macros.h"

#include "desktop_surface_presenter.h"

namespace apptraverse {

inline wchar_t const kMessengerWindowClass[] = L"AppTraverseMessengerWindow";

void RegisterMessengerWin32Classes();
void UnregisterMessengerWin32Classes();
void EnsureWin32SurfacePresenterRegistration();

class Win32SurfacePresenter : public DesktopSurfacePresenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::messenger::Win32SurfacePresenter",
      Win32SurfacePresenter, DesktopSurfacePresenter, 0)

 protected:
  Win32SurfacePresenter() = default;

 public:
  explicit Win32SurfacePresenter(ae::ObjProp prop)
      : DesktopSurfacePresenter{prop} {}

  AE_OBJECT_REFLECT()

  void OnLoad() override;
  void OnModelChanged() override;
  void OnUnload() override;

  // Snapshot outer frame via GetWindowRect and enqueue model bounds update.
  void QueueCurrentBounds();

  HWND hwnd{nullptr};

  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                  LPARAM lparam);
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_MESSENGER_WIN_PRESENTERS_H_
