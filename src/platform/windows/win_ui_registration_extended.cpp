#include "apptraverse/object_macros.h"

#include "apptraverse/presenter.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::ui::windows {
namespace {

APPTRAVERSE_REGISTER(WinEditBoxPresenter);
APPTRAVERSE_REGISTER(WinLabelPresenter);
APPTRAVERSE_REGISTER(WinBitmapImagePresenter);

}  // namespace

void EnsureExtendedWinUiPresenterRegistration() {
  EnsureExtendedUiPresenterRegistration();
  (void)&g_apptraverse_registrar_WinEditBoxPresenter;
  (void)&g_apptraverse_registrar_WinLabelPresenter;
  (void)&g_apptraverse_registrar_WinBitmapImagePresenter;
}

}  // namespace apptraverse::ui::windows
