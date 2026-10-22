#include "surfaces_demo_close_button.h"

#include <algorithm>

namespace apptraverse::example::surfaces {

void SurfacesDemoCloseButton::HandlePushButtonClicked() {
  if (!surface || !surface->surfaces) {
    return;
  }
  auto& surfaces = surface->surfaces->surfaces;
  if (surfaces.size() <= 1) {
    return;
  }
  auto const it = std::find_if(
      surfaces.begin(), surfaces.end(),
      [&](Surface::ptr const& entry) { return &*entry == &*surface; });
  if (it == surfaces.end()) {
    return;
  }
  surface->Remove();
}

}  // namespace apptraverse::example::surfaces
