#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::ui {
namespace {
APPTRAVERSE_REGISTER(UiListRowPresenter);
}
void EnsureUiPresenterRegistration() {
  EnsureUiControlRegistration();
  (void)&g_apptraverse_registrar_UiListRowPresenter;
}
}  // namespace apptraverse::ui
