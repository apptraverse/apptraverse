#include "window_catalog_win32_layout_repro_registration.h"

#include "apptraverse/object_macros.h"

#include "window_catalog_layout_push_button_presenter.h"

namespace apptraverse::example::surfaces {

namespace {

APPTRAVERSE_REGISTER(SurfacesDemoLayoutPushButtonPresenter);

}  // namespace

void EnsureSurfacesDemoWin32LayoutReproRegistration() {
  (void)&g_apptraverse_registrar_SurfacesDemoLayoutPushButtonPresenter;
}

}  // namespace apptraverse::example::surfaces
