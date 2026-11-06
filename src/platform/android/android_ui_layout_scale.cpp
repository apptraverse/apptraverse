#include "apptraverse/platform/android/android_ui_layout_scale.h"

#include <algorithm>

namespace apptraverse::ui::android {

namespace {

std::int32_t ScaleCoord(std::int32_t value, std::int32_t model_extent,
                        std::int32_t presentation_extent) {
  if (model_extent <= 0 || presentation_extent <= 0) {
    return value;
  }
  return static_cast<std::int32_t>(
      (static_cast<std::int64_t>(value) * presentation_extent) / model_extent);
}

}  // namespace

UiControlLayout ScaleControlLayoutToPresentation(
    UiControlLayout requested, std::int32_t model_client_width,
    std::int32_t model_client_height, std::int32_t presentation_width,
    std::int32_t presentation_height) {
  if (model_client_width <= 0 || model_client_height <= 0 ||
      presentation_width <= 0 || presentation_height <= 0) {
    return requested;
  }

  UiControlLayout out;
  out.control_id = requested.control_id;
  out.x = ScaleCoord(requested.x, model_client_width, presentation_width);
  out.y = ScaleCoord(requested.y, model_client_height, presentation_height);
  out.width =
      ScaleCoord(requested.width, model_client_width, presentation_width);
  out.height =
      ScaleCoord(requested.height, model_client_height, presentation_height);

  if (out.width < 1) {
    out.width = 1;
  }
  if (out.height < 1) {
    out.height = 1;
  }
  if (out.x + out.width > presentation_width) {
    out.width = std::max<std::int32_t>(1, presentation_width - out.x);
  }
  if (out.y + out.height > presentation_height) {
    out.height = std::max<std::int32_t>(1, presentation_height - out.y);
  }
  return out;
}

}  // namespace apptraverse::ui::android
