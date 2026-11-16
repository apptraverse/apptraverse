#include "window_catalog_win32_registration.h"

#include "apptraverse/object_macros.h"

#include "window_catalog_add_button.h"
#include "window_catalog_close_button.h"
#include "window_catalog_language_button.h"
#include "window_catalog_surface_window_host.h"
namespace apptraverse::example::surfaces {

namespace {

APPTRAVERSE_REGISTER(SurfacesDemoAddButton);
APPTRAVERSE_REGISTER(SurfacesDemoCloseButton);
APPTRAVERSE_REGISTER(SurfacesDemoLanguageButton);
APPTRAVERSE_REGISTER(SurfacesDemoSurfaceWindowHostPresenter);

}  // namespace

void EnsureSurfacesDemoWin32Registration() {
  (void)&g_apptraverse_registrar_SurfacesDemoAddButton;
  (void)&g_apptraverse_registrar_SurfacesDemoCloseButton;
  (void)&g_apptraverse_registrar_SurfacesDemoLanguageButton;
  (void)&g_apptraverse_registrar_SurfacesDemoSurfaceWindowHostPresenter;
}

}  // namespace apptraverse::example::surfaces
