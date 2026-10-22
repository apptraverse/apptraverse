#include "surfaces_demo_surface_window_host.h"

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

}  // namespace apptraverse::example::surfaces
