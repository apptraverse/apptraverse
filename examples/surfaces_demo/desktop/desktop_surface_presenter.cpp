#include "desktop_surface_presenter.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(DesktopSurfacePresenter);

}  // namespace

void EnsureDesktopSurfacePresenterRegistration() {
  (void)&g_apptraverse_registrar_DesktopSurfacePresenter;
}

}  // namespace apptraverse
