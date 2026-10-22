#include "surfaces_demo_surface_window_host.h"

#include "apptraverse/platform/windows/win32_fatal.h"

#include "surfaces_demo_win32_child_command.h"

namespace apptraverse::example::surfaces {

bool SurfacesDemoSurfaceWindowHostPresenter::ReadyForPresentation() const {
  return surface_window && surface_window->presentation_loaded;
}

bool SurfacesDemoSurfaceWindowHostPresenter::PresentationCreateSucceeded()
    const {
  return surface_window && surface_window->hwnd != nullptr;
}

void* SurfacesDemoSurfaceWindowHostPresenter::NativeChildParentHandle()
    const {
  if (!surface_window || surface_window->hwnd == nullptr) {
    return nullptr;
  }
  return reinterpret_cast<void*>(surface_window->hwnd);
}

ui::UiControlLayout SurfacesDemoSurfaceWindowHostPresenter::ResolveChildLayout(
    ui::UiControlLayout requested) const {
  if (!surface_window) {
    return requested;
  }
  if (requested.control_id !=
          static_cast<std::uint32_t>(kSurfaceLanguageButtonId) &&
      requested.control_id !=
          static_cast<std::uint32_t>(kSurfaceCloseButtonId)) {
    return requested;
  }
  ui::UiControlLayout resolved = requested;
  ComputeSurfaceChromeButtonLayout(*surface_window, requested.control_id,
                                   &resolved.x, &resolved.y, &resolved.width,
                                   &resolved.height);
  return resolved;
}

void SurfacesDemoSurfaceWindowHostPresenter::OnLoad() {
  if (!surface_window) {
    FatalWin32("SurfacesDemoSurfaceWindowHostPresenter OnLoad surface_window",
               ERROR_INVALID_PARAMETER);
  }
  if (surface_window->hwnd == nullptr) {
    FatalWin32("SurfacesDemoSurfaceWindowHostPresenter OnLoad parent hwnd",
               ERROR_INVALID_PARAMETER);
  }
  surface_window->child_command_handler =
      &TryHandleDemoLibraryCloseChildCommand;
}

void SurfacesDemoSurfaceWindowHostPresenter::OnUnload() {
  if (surface_window &&
      surface_window->child_command_handler ==
          &TryHandleDemoLibraryCloseChildCommand) {
    surface_window->child_command_handler = nullptr;
  }
}

}  // namespace apptraverse::example::surfaces
