#include "window_catalog_chrome_layout.h"

#include <algorithm>

namespace apptraverse {
namespace {

constexpr std::int32_t kBaseMargin = 12;
constexpr std::int32_t kBaseButtonHeight = 48;
constexpr std::int32_t kBaseGap = 8;
constexpr double kBaseShortSide = 480.0;
constexpr double kMaxScale = 3.0;
constexpr int kButtonCount = 4;

std::int32_t AtLeastOne(std::int32_t value) { return std::max(value, 1); }

WindowCatalogChromeRect MakeRect(std::int32_t x, std::int32_t y,
                                 std::int32_t width, std::int32_t height) {
  return {x, y, AtLeastOne(width), AtLeastOne(height)};
}

}  // namespace

WindowCatalogMainChromeLayout ComputeWindowCatalogMainChromeLayout(
    std::int32_t client_w, std::int32_t client_h) {
  std::int32_t const w = AtLeastOne(client_w);
  std::int32_t const h = AtLeastOne(client_h);

  double const short_side = static_cast<double>(std::min(w, h));
  double const scale =
      std::clamp(short_side / kBaseShortSide, 1.0, kMaxScale);
  std::int32_t const margin = static_cast<std::int32_t>(kBaseMargin * scale);
  std::int32_t const gap = static_cast<std::int32_t>(kBaseGap * scale);
  std::int32_t const button_h =
      static_cast<std::int32_t>(kBaseButtonHeight * scale);
  std::int32_t const column_h =
      kButtonCount * button_h + (kButtonCount - 1) * gap;

  WindowCatalogMainChromeLayout layout;
  WindowCatalogChromeRect* buttons[kButtonCount] = {
      &layout.add, &layout.open, &layout.delete_button, &layout.language};

  if (h >= w) {
    std::int32_t const inner_w = w - 2 * margin;
    std::int32_t const buttons_top = h - margin - column_h;
    layout.list = MakeRect(margin, margin, inner_w,
                           buttons_top - margin - margin);
    for (int i = 0; i < kButtonCount; ++i) {
      *buttons[i] = MakeRect(margin, buttons_top + i * (button_h + gap),
                             inner_w, button_h);
    }
    return layout;
  }

  std::int32_t const list_w = (w - 3 * margin) / 2;
  std::int32_t const buttons_x = 2 * margin + list_w;
  std::int32_t const buttons_w = w - buttons_x - margin;
  layout.list = MakeRect(margin, margin, list_w, h - 2 * margin);
  for (int i = 0; i < kButtonCount; ++i) {
    *buttons[i] = MakeRect(buttons_x, margin + i * (button_h + gap), buttons_w,
                           button_h);
  }
  return layout;
}

}  // namespace apptraverse
