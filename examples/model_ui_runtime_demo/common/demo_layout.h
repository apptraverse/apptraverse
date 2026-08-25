#ifndef APPTRAVERSE_DEMO_LAYOUT_H_
#define APPTRAVERSE_DEMO_LAYOUT_H_

#include <cstdint>

#include "demo_model.h"

namespace apptraverse {

struct NativeRect {
  std::int32_t x{0};
  std::int32_t y{0};
  std::int32_t width{0};
  std::int32_t height{0};

  bool operator==(NativeRect const& other) const {
    return x == other.x && y == other.y && width == other.width &&
           height == other.height;
  }
};

inline NativeRect TextToolbarNativeRect(LayoutWindow const& window) {
  return NativeRect{0, 0, window.client_width, window.text_toolbar->height};
}

inline NativeRect ColorToolbarNativeRect(LayoutWindow const& window) {
  return NativeRect{0, window.text_toolbar->height, window.client_width,
                    window.color_toolbar->height};
}

inline NativeRect CenterStripNativeRect(LayoutWindow const& window) {
  std::int32_t const y =
      window.text_toolbar->height + window.color_toolbar->height;
  std::int32_t width = static_cast<std::int32_t>(
      (static_cast<std::uint32_t>(window.client_width) *
       window.center_strip->width_numerator) /
      window.center_strip->width_denominator);
  if (width < 1) {
    width = 1;
  }
  std::int32_t x = (window.client_width - width) / 2;
  if (x < 0) {
    x = 0;
  }
  std::int32_t height = window.client_height - y;
  if (height < 1) {
    height = 1;
  }
  return NativeRect{x, y, width, height};
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_DEMO_LAYOUT_H_
