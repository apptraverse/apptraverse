#include "apptraverse/platform/web/web_ui_window_presenter.h"

#include "apptraverse/object_macros.h"
#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::ui::web {
namespace {

APPTRAVERSE_REGISTER(WebDesktopWindowPresenter);

}  // namespace

void EnsureWebUiPresenterRegistration() {
  EnsureUiPresenterRegistration();
  (void)&g_apptraverse_registrar_WebDesktopWindowPresenter;
}

}  // namespace apptraverse::ui::web
