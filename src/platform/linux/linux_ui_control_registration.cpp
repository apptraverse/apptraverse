#include "apptraverse/platform/linux/linux_ui_registration.h"
#include "apptraverse/platform/linux/linux_ui_text_control_presenter.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::ui::linux {
namespace {
APPTRAVERSE_REGISTER(LinuxTextControlPresenter);
}
void EnsureLinuxUiPresenterRegistration() {
  EnsureLinuxWindowControlPresenterRegistration();
  EnsureUiControlRegistration();
  (void)&g_apptraverse_registrar_LinuxTextControlPresenter;
}
}  // namespace apptraverse::ui::linux
