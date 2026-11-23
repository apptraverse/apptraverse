#ifndef APPTRAVERSE_EXAMPLE_MINIMAL_WEB_REGISTRATION_H_
#define APPTRAVERSE_EXAMPLE_MINIMAL_WEB_REGISTRATION_H_

#include "minimal_application.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/platform/web/web_ui_window_presenter.h"

namespace apptraverse::example::minimal {
inline void RegisterWebMinimalApplicationTypes() {
  EnsureCoreObjectRegistration();
  ui::web::EnsureWebWindowControlRegistration();
  EnsureMinimalApplicationRegistration();
}
}  // namespace apptraverse::example::minimal

#endif
