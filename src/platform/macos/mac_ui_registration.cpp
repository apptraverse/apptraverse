#include "apptraverse/object_macros.h"

#include "apptraverse/platform/macos/mac_ui_presenters.h"
#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::ui::macos {
namespace {

APPTRAVERSE_REGISTER(MacDesktopWindowPresenter);
APPTRAVERSE_REGISTER(MacPushButtonPresenter);
APPTRAVERSE_REGISTER(MacListContainerPresenter);

}  // namespace

void EnsureMacUiPresenterRegistration() {
  EnsureUiPresenterRegistration();
  (void)&g_apptraverse_registrar_MacDesktopWindowPresenter;
  (void)&g_apptraverse_registrar_MacPushButtonPresenter;
  (void)&g_apptraverse_registrar_MacListContainerPresenter;
}

}  // namespace apptraverse::ui::macos
