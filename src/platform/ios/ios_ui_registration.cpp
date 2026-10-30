#include "apptraverse/object_macros.h"

#include "apptraverse/platform/ios/ios_ui_presenters.h"
#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::ui::ios {
namespace {

APPTRAVERSE_REGISTER(IosDesktopWindowPresenter);
APPTRAVERSE_REGISTER(IosPushButtonPresenter);
APPTRAVERSE_REGISTER(IosListContainerPresenter);

}  // namespace

void EnsureIosUiPresenterRegistration() {
  EnsureUiPresenterRegistration();
  (void)&g_apptraverse_registrar_IosDesktopWindowPresenter;
  (void)&g_apptraverse_registrar_IosPushButtonPresenter;
  (void)&g_apptraverse_registrar_IosListContainerPresenter;
}

}  // namespace apptraverse::ui::ios
