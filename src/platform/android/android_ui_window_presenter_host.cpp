#include "apptraverse/platform/android/android_ui_window_presenter.h"

// Build-host (distill exporter) definition. The device build links
// android_ui_window_presenter.cpp instead, which scales child layouts to the
// measured client mount. The host has no views, so layouts pass through.
namespace apptraverse::ui::android {

UiControlLayout AndroidDesktopWindowPresenter::ResolveChildLayout(
    UiControlLayout requested) const {
  return requested;
}

}  // namespace apptraverse::ui::android
