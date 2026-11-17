#include "apptraverse/object_macros.h"

#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/ui/ui_presenters.h"
#include "apptraverse/platform/windows/win_ui_text_control_presenter.h"

namespace apptraverse::ui::windows {
namespace {

APPTRAVERSE_REGISTER(WinDesktopWindowPresenter);
APPTRAVERSE_REGISTER(WinTextControlPresenter);
APPTRAVERSE_REGISTER(WinPushButtonPresenter);
APPTRAVERSE_REGISTER(WinListContainerPresenter);

}  // namespace

void EnsureWinControlPresenterRegistration() {
  EnsureUiControlRegistration();
  (void)&g_apptraverse_registrar_WinTextControlPresenter;
  (void)&g_apptraverse_registrar_WinDesktopWindowPresenter;
  (void)&g_apptraverse_registrar_WinPushButtonPresenter;
  (void)&g_apptraverse_registrar_WinListContainerPresenter;
}

}  // namespace apptraverse::ui::windows
