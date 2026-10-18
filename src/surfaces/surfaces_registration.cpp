#include "apptraverse/surfaces/surfaces_registration.h"

#include "apptraverse/object_macros.h"

#include "apptraverse/surfaces/desktop_surface_presenter.h"
#include "apptraverse/surfaces/surface_button_presenter.h"
#include "apptraverse/surfaces/surface_language_button_presenter.h"
#include "apptraverse/surfaces/surface_window_presenter.h"
#include "apptraverse/surfaces/surfaces_model.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(Surface);
APPTRAVERSE_REGISTER(SurfacePresenter);
APPTRAVERSE_REGISTER(Surfaces);
APPTRAVERSE_REGISTER(AddSurfaceEvent);
APPTRAVERSE_REGISTER(RemoveSurfaceEvent);
APPTRAVERSE_REGISTER(SetCurrentSurfaceEvent);
APPTRAVERSE_REGISTER(SurfaceBoundsChangedEvent);
APPTRAVERSE_REGISTER(SurfacePresentationSizeChangedEvent);
APPTRAVERSE_REGISTER(Application);

}  // namespace

void EnsureSurfacesRegistration() {
  (void)&g_apptraverse_registrar_Application;
  (void)&g_apptraverse_registrar_Surface;
  (void)&g_apptraverse_registrar_SurfaceBoundsChangedEvent;
  (void)&g_apptraverse_registrar_SurfacePresentationSizeChangedEvent;
  EnsureDesktopSurfacePresenterRegistration();
  EnsureSurfaceWindowPresenterRegistration();
  EnsureSurfaceButtonPresenterRegistration();
  EnsureSurfaceLanguageButtonPresenterRegistration();
}

}  // namespace apptraverse
