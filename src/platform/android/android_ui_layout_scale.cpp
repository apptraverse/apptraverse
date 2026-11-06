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

namespace {

std::int32_t DpMinHeightPx(std::int32_t density_dpi) {
  constexpr std::int32_t kMdpi = 160;
  constexpr std::int32_t kMinTouchDp = 48;
  if (density_dpi <= 0) {
    density_dpi = kMdpi;
  }
  return (kMinTouchDp * density_dpi + kMdpi / 2) / kMdpi;
}

}  // namespace

UiControlLayout ScaleControlLayoutToPresentation(
    UiControlLayout requested, std::int32_t model_client_width,
    std::int32_t model_client_height, std::int32_t presentation_width,
    std::int32_t presentation_height, std::int32_t density_dpi) {
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

  std::int32_t const min_h = DpMinHeightPx(density_dpi);
  if (out.height < min_h) {
    out.height = min_h;
  }

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
    std::int32_t const max_h = std::max<std::int32_t>(1, presentation_height - out.y);
    out.height = std::min(out.height, max_h);
    // Keep at least a readable control height when the viewport clips the bottom;
    // shrink width first so the label can wrap instead of crushing height to a hairline.
    if (out.height < min_h) {
      std::int32_t const deficit = min_h - out.height;
      std::int32_t target_w = out.width - deficit;
      if (target_w < min_h) {
        target_w = min_h;
      }
      if (out.x + target_w > presentation_width) {
        target_w = std::max<std::int32_t>(min_h, presentation_width - out.x);
      }
      out.width = target_w;
      out.height = std::min(min_h, presentation_height - out.y);
    }
  }
  return out;
}

}  // namespace apptraverse::ui::android
