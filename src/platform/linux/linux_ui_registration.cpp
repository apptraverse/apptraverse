#include "apptraverse/platform/linux/linux_ui_registration.h"

#include "apptraverse/platform/linux/linux_ui_list_presenter.h"
#include "apptraverse/platform/linux/linux_ui_push_button_presenter.h"
#include "apptraverse/platform/linux/linux_ui_window_presenter.h"

#include "apptraverse/object_macros.h"
#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::ui::linux {
namespace {

APPTRAVERSE_REGISTER(LinuxDesktopWindowPresenter);
APPTRAVERSE_REGISTER(LinuxPushButtonPresenter);
APPTRAVERSE_REGISTER(LinuxListContainerPresenter);

}  // namespace

void EnsureLinuxUiPresenterRegistration() {
  EnsureUiControlRegistration();
  (void)&g_apptraverse_registrar_LinuxDesktopWindowPresenter;
  (void)&g_apptraverse_registrar_LinuxPushButtonPresenter;
  (void)&g_apptraverse_registrar_LinuxListContainerPresenter;
}

}  // namespace apptraverse::ui::linux
