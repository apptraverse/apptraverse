#include "apptraverse/object_macros.h"

#include "apptraverse/presenter.h"
#include "apptraverse/platform/windows/win_ui_extended_child_routing.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::ui::windows {
namespace {

constexpr std::uint16_t kEnChange = 0x0300;

bool RouteExtendedChildCommand(std::uint16_t notification_code,
                               Presenter* child_presenter) {
  if (notification_code != kEnChange || child_presenter == nullptr) {
    return false;
  }
  static_cast<WinEditBoxPresenter*>(child_presenter)->ReadFromNativeAndSubmit();
  return true;
}

APPTRAVERSE_REGISTER(WinEditBoxPresenter);
APPTRAVERSE_REGISTER(WinLabelPresenter);
APPTRAVERSE_REGISTER(WinBitmapImagePresenter);

}  // namespace

void EnsureExtendedWinUiPresenterRegistration() {
  EnsureExtendedUiPresenterRegistration();
  SetExtendedChildCommandRouter(&RouteExtendedChildCommand);
  (void)&g_apptraverse_registrar_WinEditBoxPresenter;
  (void)&g_apptraverse_registrar_WinLabelPresenter;
  (void)&g_apptraverse_registrar_WinBitmapImagePresenter;
}

}  // namespace apptraverse::ui::windows
