#include "surfaces_demo_win32_registration.h"

#include "apptraverse/object_macros.h"

#include "surfaces_demo_add_button.h"
#include "surfaces_demo_surface_window_host.h"

namespace apptraverse::example::surfaces {

namespace {

APPTRAVERSE_REGISTER(SurfacesDemoAddButton);
APPTRAVERSE_REGISTER(SurfacesDemoSurfaceWindowHostPresenter);

}  // namespace

void EnsureSurfacesDemoWin32Registration() {
  (void)&g_apptraverse_registrar_SurfacesDemoAddButton;
  (void)&g_apptraverse_registrar_SurfacesDemoSurfaceWindowHostPresenter;
}

}  // namespace apptraverse::example::surfaces
