#include "mobile_surface_presenter.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(MobileSurfacePresenter);

}  // namespace

void EnsureMobileSurfacePresenterRegistration() {
  (void)&g_apptraverse_registrar_MobileSurfacePresenter;
}

std::string MobileSurfacePresenter::PageTitle() const {
  return "Surface " + std::to_string(surface->number);
}

bool MobileSurfacePresenter::RemovableFromPager() const {
  return surface->surfaces->surfaces.size() > 1;
}

}  // namespace apptraverse
