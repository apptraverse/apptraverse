#include "apptraverse/object_macros.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"

namespace apptraverse::ui::windows {
namespace {
APPTRAVERSE_REGISTER(WinWindowPresenter);
}
void EnsureWinUiPresenterRegistration() {
  EnsureUiPresenterRegistration();
  EnsureWinControlPresenterRegistration();
  (void)&g_apptraverse_registrar_WinWindowPresenter;
}
}  // namespace apptraverse::ui::windows
