#include "apptraverse/platform/android/android_ui_registration.h"
#include "apptraverse/platform/android/android_ui_text_control_presenter.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::ui::android {
namespace {
APPTRAVERSE_REGISTER(AndroidTextControlPresenter);
}

void EnsureAndroidUiPresenterRegistration() {
  EnsureAndroidWindowControlPresenterRegistration();
  EnsureUiPresenterRegistration();
  (void)&g_apptraverse_registrar_AndroidTextControlPresenter;
}
}  // namespace apptraverse::ui::android
