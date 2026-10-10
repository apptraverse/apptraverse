#include "apptraverse/object_macros.h"

#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::ui::windows {
namespace {

APPTRAVERSE_REGISTER(WinWindowPresenter);
APPTRAVERSE_REGISTER(WinDesktopWindowPresenter);
APPTRAVERSE_REGISTER(WinEditBoxPresenter);
APPTRAVERSE_REGISTER(WinPushButtonPresenter);
APPTRAVERSE_REGISTER(WinListContainerPresenter);
APPTRAVERSE_REGISTER(WinLabelPresenter);
APPTRAVERSE_REGISTER(WinBitmapImagePresenter);

}  // namespace

void EnsureWinUiPresenterRegistration() {
  EnsureUiPresenterRegistration();
  (void)&g_apptraverse_registrar_WinDesktopWindowPresenter;
  (void)&g_apptraverse_registrar_WinEditBoxPresenter;
  (void)&g_apptraverse_registrar_WinPushButtonPresenter;
  (void)&g_apptraverse_registrar_WinListContainerPresenter;
  (void)&g_apptraverse_registrar_WinLabelPresenter;
  (void)&g_apptraverse_registrar_WinBitmapImagePresenter;
}

}  // namespace apptraverse::ui::windows
