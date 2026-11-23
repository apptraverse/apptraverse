#ifndef APPTRAVERSE_EXAMPLE_MINIMAL_LINUX_REGISTRATION_H_
#define APPTRAVERSE_EXAMPLE_MINIMAL_LINUX_REGISTRATION_H_

#include "minimal_application.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/platform/linux/linux_ui_registration.h"

namespace apptraverse::example::minimal {
inline void RegisterLinuxMinimalApplicationTypes() {
  EnsureCoreObjectRegistration();
  ui::linux::EnsureLinuxWindowControlPresenterRegistration();
  EnsureMinimalApplicationRegistration();
}
}  // namespace apptraverse::example::minimal

#endif
