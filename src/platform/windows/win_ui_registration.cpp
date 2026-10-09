#include "apptraverse/object_macros.h"

#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::ui::windows {
namespace {

APPTRAVERSE_REGISTER(WinWindowPresenter);
APPTRAVERSE_REGISTER(WinDesktopWindowPresenter);
APPTRAVERSE_REGISTER(WinEditBoxPresenter);
APPTRAVERSE_REGISTER(WinPushButtonPresenter);
APPTRAVERSE_REGISTER(WinLabelPresenter);

}  // namespace

void EnsureWinUiPresenterRegistration() {
  EnsureUiPresenterRegistration();
  (void)&g_apptraverse_registrar_WinDesktopWindowPresenter;
  (void)&g_apptraverse_registrar_WinEditBoxPresenter;
  (void)&g_apptraverse_registrar_WinPushButtonPresenter;
  (void)&g_apptraverse_registrar_WinLabelPresenter;
}

}  // namespace apptraverse::ui::windows
