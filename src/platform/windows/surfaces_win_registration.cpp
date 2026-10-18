#include "apptraverse/platform/windows/surfaces_win_registration.h"

#include "apptraverse/object_macros.h"
#include "apptraverse/platform/windows/surfaces_win_presenters.h"
#include "apptraverse/surfaces/surfaces_registration.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(Win32SurfaceWindowPresenter);
APPTRAVERSE_REGISTER(Win32SurfaceButtonPresenter);
APPTRAVERSE_REGISTER(Win32SurfaceLanguageButtonPresenter);

}  // namespace

void EnsureWin32SurfacesPresenterRegistration() {
  EnsureSurfacesRegistration();
  (void)&g_apptraverse_registrar_Win32SurfaceWindowPresenter;
  (void)&g_apptraverse_registrar_Win32SurfaceButtonPresenter;
  (void)&g_apptraverse_registrar_Win32SurfaceLanguageButtonPresenter;
}

}  // namespace apptraverse
