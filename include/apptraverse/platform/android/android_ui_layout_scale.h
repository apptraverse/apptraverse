#ifndef APPTRAVERSE_PLATFORM_ANDROID_ANDROID_UI_LAYOUT_SCALE_H_
#define APPTRAVERSE_PLATFORM_ANDROID_ANDROID_UI_LAYOUT_SCALE_H_

#include "apptraverse/ui/ui_window_presenter.h"

namespace apptraverse::ui::android {

// Maps model client-space chrome layout to the measured Android client mount
// (physical px). Model sizes are desktop client units, not dp.
UiControlLayout ScaleControlLayoutToPresentation(
    UiControlLayout requested, std::int32_t model_client_width,
    std::int32_t model_client_height, std::int32_t presentation_width,
    std::int32_t presentation_height);

}  // namespace apptraverse::ui::android

#endif  // APPTRAVERSE_PLATFORM_ANDROID_ANDROID_UI_LAYOUT_SCALE_H_
