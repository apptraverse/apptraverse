#ifndef APPTRAVERSE_EXAMPLE_MINIMAL_ANDROID_REGISTRATION_H_
#define APPTRAVERSE_EXAMPLE_MINIMAL_ANDROID_REGISTRATION_H_

#include "minimal_application.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/platform/android/android_ui_registration.h"

namespace apptraverse::example::minimal {
inline void RegisterAndroidMinimalApplicationTypes() {
  EnsureCoreObjectRegistration();
  ui::android::EnsureAndroidWindowControlPresenterRegistration();
  EnsureMinimalApplicationRegistration();
}
}  // namespace apptraverse::example::minimal

#endif
