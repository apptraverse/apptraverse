#include "mobile_surface_presenter.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(MobileSurfacePresenter);

}  // namespace

void EnsureMobileSurfacePresenterRegistration() {
  (void)&g_apptraverse_registrar_MobileSurfacePresenter;
}

}  // namespace apptraverse
