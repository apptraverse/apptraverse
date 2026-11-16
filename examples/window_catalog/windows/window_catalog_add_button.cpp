#include "window_catalog_add_button.h"

namespace apptraverse::example::surfaces {

void SurfacesDemoAddButton::HandlePushButtonClicked() {
  if (surface) {
    surface->AddSurface();
  }
}

}  // namespace apptraverse::example::surfaces
