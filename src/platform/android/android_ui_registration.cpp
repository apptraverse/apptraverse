#include "apptraverse/platform/android/android_ui_list_presenter.h"
#include "apptraverse/platform/android/android_ui_push_button_presenter.h"
#include "apptraverse/platform/android/android_ui_window_presenter.h"

#include "apptraverse/object_macros.h"
#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::ui::android {
namespace {

APPTRAVERSE_REGISTER(AndroidDesktopWindowPresenter);
APPTRAVERSE_REGISTER(AndroidPushButtonPresenter);
APPTRAVERSE_REGISTER(AndroidListContainerPresenter);

}  // namespace

void EnsureAndroidUiPresenterRegistration() {
  EnsureUiPresenterRegistration();
  (void)&g_apptraverse_registrar_AndroidDesktopWindowPresenter;
  (void)&g_apptraverse_registrar_AndroidPushButtonPresenter;
  (void)&g_apptraverse_registrar_AndroidListContainerPresenter;
}

}  // namespace apptraverse::ui::android
