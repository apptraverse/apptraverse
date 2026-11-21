#include "apptraverse/platform/android/android_ui_text_control_presenter.h"
#include "apptraverse/platform/android/android_ui_native_views.h"

namespace apptraverse::ui::android {
namespace {
AndroidUiNativeViews& NativeViews(Presenter const& presenter) {
  return *static_cast<AndroidUiNativeViews*>(presenter.presentation_host);
}
}  // namespace

void AndroidTextControlPresenter::OnLoad() {
  NativeViews(*this).LoadTextControl(*this);
}
void AndroidTextControlPresenter::OnModelChanged() {
  NativeViews(*this).SyncTextControl(*this);
}
void AndroidTextControlPresenter::OnUnload() {
  NativeViews(*this).UnloadTextControl(*this);
}

}  // namespace apptraverse::ui::android
