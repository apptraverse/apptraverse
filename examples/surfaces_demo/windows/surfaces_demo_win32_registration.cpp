#include "surfaces_demo_win32_registration.h"

#include "apptraverse/object_macros.h"

#include "surfaces_demo_add_button.h"
#include "surfaces_demo_close_button.h"
#include "surfaces_demo_language_button.h"
#include "surfaces_demo_surface_window_host.h"
#include "surfaces_demo_win32_child_command.h"

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
  EnsureSurfacesDemoWin32ChildCommandHook();
}

}  // namespace apptraverse::example::surfaces
