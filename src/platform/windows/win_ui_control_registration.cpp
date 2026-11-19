#include "apptraverse/object_macros.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/platform/windows/win_ui_text_control_presenter.h"

namespace apptraverse::ui::windows {
namespace {

APPTRAVERSE_REGISTER(WinTextControlPresenter);

}  // namespace

void EnsureWinControlPresenterRegistration() {
  EnsureWinWindowControlPresenterRegistration();
  EnsureUiControlRegistration();
  (void)&g_apptraverse_registrar_WinTextControlPresenter;
}

}  // namespace apptraverse::ui::windows
