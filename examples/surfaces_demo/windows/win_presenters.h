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
inline constexpr int kSurfaceCloseButtonId = 1002;

void RegisterSurfacesWin32Classes();
void UnregisterSurfacesWin32Classes();
void EnsureWin32SurfacePresenterRegistration();

// Child WM_COMMAND → Presenter::OnCommand(command_id, notification).
// Parent WndProc stays free of Add/Close semantics. Child HWND GWLP_USERDATA
// holds Presenter*.
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
  bool OnCommand(std::uint32_t command_id,
                 std::uint16_t notification_code) override;

  // Snapshot outer frame via GetWindowRect and enqueue model bounds update.
  void QueueCurrentBounds();

  HWND hwnd{nullptr};
  HWND add_button{nullptr};
  HWND close_button{nullptr};

  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                  LPARAM lparam);

 private:
  // Place Add / Close from SurfacePresenter::IsWide after model publication.
  // Not model state. Client size is reported via PresentationSizeChanged.
  void LayoutControls();
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_SURFACES_WIN_PRESENTERS_H_
