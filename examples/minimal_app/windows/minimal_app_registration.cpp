#include "minimal_app_registration.h"

#include "minimal_application.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"

namespace apptraverse::example::minimal {
void RegisterMinimalApplicationTypes() {
  EnsureCoreObjectRegistration();
  ui::windows::EnsureWinControlPresenterRegistration();
  EnsureMinimalApplicationRegistration();
}
}  // namespace apptraverse::example::minimal
