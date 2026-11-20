#include "apptraverse/platform/web/web_ui_list_presenter.h"
#include "apptraverse/platform/web/web_ui_push_button_presenter.h"
#include "apptraverse/platform/web/web_ui_window_presenter.h"

#include "apptraverse/platform/web/web_ui_text_control_presenter.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::ui::web {
namespace {

APPTRAVERSE_REGISTER(WebTextControlPresenter);

}  // namespace

void EnsureWebUiPresenterRegistration() {
  EnsureUiPresenterRegistration();
  EnsureWebWindowControlRegistration();
  (void)&g_apptraverse_registrar_WebTextControlPresenter;
}

}  // namespace apptraverse::ui::web
