#include "apptraverse/platform/android/android_ui_window_presenter.h"

// Build-host (distill exporter) definition. The device build links
// android_ui_window_presenter.cpp instead. Both apply model-computed pixel
// layouts directly; viewport/DPI changes reach the model through Events.
namespace apptraverse::ui::android {

UiControlLayout AndroidDesktopWindowPresenter::ResolveChildLayout(
    UiControlLayout requested) const {
  return requested;
}

}  // namespace apptraverse::ui::android
