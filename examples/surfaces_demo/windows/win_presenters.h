#ifndef APPTRAVERSE_SURFACES_WIN_PRESENTERS_H_
#define APPTRAVERSE_SURFACES_WIN_PRESENTERS_H_

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif

#include <cstdint>

#include "apptraverse/object_macros.h"

#include "desktop_surface_presenter.h"

namespace apptraverse {

inline wchar_t const kSurfacesWindowClass[] = L"AppTraverseSurfacesWindow";
inline constexpr int kSurfaceAddButtonId = 1001;

void RegisterSurfacesWin32Classes();
void UnregisterSurfacesWin32Classes();
void EnsureWin32SurfacePresenterRegistration();

// Child WM_COMMAND → Presenter::OnCommand. Parent WndProc stays free of Add
// semantics. Child HWND GWLP_USERDATA holds Presenter*.
bool DispatchChildCommand(WPARAM wparam, LPARAM lparam);

class Win32SurfacePresenter : public DesktopSurfacePresenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::Win32SurfacePresenter",
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
  bool OnCommand(std::uint16_t notification_code) override;

  HWND hwnd{nullptr};
  HWND add_button{nullptr};

  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                  LPARAM lparam);
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_SURFACES_WIN_PRESENTERS_H_
