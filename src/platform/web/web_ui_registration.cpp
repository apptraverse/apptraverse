#include "apptraverse/platform/web/web_ui_list_presenter.h"
#include "apptraverse/platform/web/web_ui_push_button_presenter.h"
#include "apptraverse/platform/web/web_ui_window_presenter.h"

#include "apptraverse/object_macros.h"
#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::ui::web {
namespace {

APPTRAVERSE_REGISTER(WebDesktopWindowPresenter);
APPTRAVERSE_REGISTER(WebPushButtonPresenter);
APPTRAVERSE_REGISTER(WebListContainerPresenter);

}  // namespace

void EnsureWebUiPresenterRegistration() {
  EnsureUiPresenterRegistration();
  (void)&g_apptraverse_registrar_WebDesktopWindowPresenter;
  (void)&g_apptraverse_registrar_WebPushButtonPresenter;
  (void)&g_apptraverse_registrar_WebListContainerPresenter;
}

}  // namespace apptraverse::ui::web
