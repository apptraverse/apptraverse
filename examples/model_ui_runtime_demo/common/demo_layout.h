#ifndef APPTRAVERSE_DEMO_LAYOUT_H_
#define APPTRAVERSE_DEMO_LAYOUT_H_

#include <cassert>
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

inline NativeRect CenterStripNativeRect(LayoutWindow const& window,
                                        std::size_t index) {
  assert(!window.center_strips.empty());
  assert(index < window.center_strips.size());
  auto const& strip = window.center_strips[index];
  assert(strip.is_valid());
  strip.Load();

  std::int32_t const content_top =
      window.text_toolbar->height + window.color_toolbar->height;
  std::int32_t content_height = window.client_height - content_top;
  if (content_height < 1) {
    content_height = 1;
  }
  auto const n = window.center_strips.size();
  std::int32_t const base_height =
      content_height / static_cast<std::int32_t>(n);
  std::int32_t height = base_height;
  if (index + 1 == n) {
    height = content_height - base_height * static_cast<std::int32_t>(index);
  }
  if (height < 1) {
    height = 1;
  }
  std::int32_t const y =
      content_top + base_height * static_cast<std::int32_t>(index);

  std::int32_t width = static_cast<std::int32_t>(
      (static_cast<std::uint32_t>(window.client_width) *
       strip->width_numerator) /
      strip->width_denominator);
  if (width < 1) {
    width = 1;
  }
  std::int32_t x = (window.client_width - width) / 2;
  if (x < 0) {
    x = 0;
  }
  return NativeRect{x, y, width, height};
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_DEMO_LAYOUT_H_
