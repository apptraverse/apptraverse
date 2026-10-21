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

}  // namespace apptraverse::example::surfaces
