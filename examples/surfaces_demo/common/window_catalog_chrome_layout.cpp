#include "window_catalog_chrome_layout.h"

#include <algorithm>

namespace apptraverse {
namespace {

constexpr std::int32_t kMdpiDpr = 160;
constexpr std::int32_t kMarginDp = 12;
constexpr std::int32_t kGapDp = 8;
constexpr std::int32_t kButtonMinHeightDp = 48;
constexpr int kButtonCount = 4;

std::int32_t AtLeastOne(std::int32_t value) { return std::max(value, 1); }

std::int32_t DpToModelPx(std::int32_t dp, std::int32_t density_dpi) {
  if (density_dpi <= 0) {
    density_dpi = kMdpiDpr;
  }
  return static_cast<std::int32_t>(
      (static_cast<std::int64_t>(dp) * density_dpi + kMdpiDpr / 2) / kMdpiDpr);
}

WindowCatalogChromeRect MakeRect(std::int32_t x, std::int32_t y,
                                 std::int32_t width, std::int32_t height) {
  return {x, y, AtLeastOne(width), AtLeastOne(height)};
}

}  // namespace

WindowCatalogMainChromeLayout ComputeWindowCatalogMainChromeLayout(
    std::int32_t client_w, std::int32_t client_h, std::int32_t density_dpi) {
  std::int32_t const w = AtLeastOne(client_w);
  std::int32_t const h = AtLeastOne(client_h);

  std::int32_t const margin = DpToModelPx(kMarginDp, density_dpi);
  std::int32_t const gap_base = DpToModelPx(kGapDp, density_dpi);
  std::int32_t const button_h_base = DpToModelPx(kButtonMinHeightDp, density_dpi);
  std::int32_t const column_h_base =
      kButtonCount * button_h_base + (kButtonCount - 1) * gap_base;

  WindowCatalogMainChromeLayout layout;
  WindowCatalogChromeRect* buttons[kButtonCount] = {
      &layout.add, &layout.open, &layout.delete_button, &layout.language};

  if (h > w) {
    std::int32_t const inner_w = w - 2 * margin;
    std::int32_t const list_top = margin;
    std::int32_t const list_bottom_max = h - margin - margin;
    std::int32_t const list_h_max = std::max(1, list_bottom_max - list_top);
    double scale = 1.0;
    if (column_h_base > list_h_max) {
      double const denom = static_cast<double>(column_h_base);
      scale = static_cast<double>(list_h_max) / denom;
    }
    std::int32_t const gap =
        std::max(0, static_cast<std::int32_t>(gap_base * scale + 0.5));
    std::int32_t const button_h =
        std::max(1, static_cast<std::int32_t>(button_h_base * scale + 0.5));
    std::int32_t const column_h =
        kButtonCount * button_h + (kButtonCount - 1) * gap;
    std::int32_t const buttons_top = h - margin - column_h;
    std::int32_t const list_h = std::max(1, buttons_top - margin - margin);
    layout.list = MakeRect(margin, margin, inner_w, list_h);
    for (int i = 0; i < kButtonCount; ++i) {
      *buttons[i] = MakeRect(margin, buttons_top + i * (button_h + gap),
                             inner_w, button_h);
    }
    return layout;
  }

  std::int32_t const list_w = (w - 3 * margin) / 2;
  std::int32_t const buttons_x = 2 * margin + list_w;
  std::int32_t const buttons_w = w - buttons_x - margin;
  std::int32_t const buttons_area_h = h - 2 * margin;
  double scale = 1.0;
  if (column_h_base > buttons_area_h) {
    scale = static_cast<double>(buttons_area_h) /
            static_cast<double>(column_h_base);
  }
  std::int32_t const gap =
      std::max(0, static_cast<std::int32_t>(gap_base * scale + 0.5));
  std::int32_t const button_h =
      std::max(1, static_cast<std::int32_t>(button_h_base * scale + 0.5));
  std::int32_t const column_h =
      kButtonCount * button_h + (kButtonCount - 1) * gap;
  std::int32_t const buttons_y0 =
      margin + std::max(0, (buttons_area_h - column_h) / 2);
  layout.list = MakeRect(margin, margin, list_w, h - 2 * margin);
  for (int i = 0; i < kButtonCount; ++i) {
    *buttons[i] = MakeRect(buttons_x, buttons_y0 + i * (button_h + gap), buttons_w,
                           button_h);
  }
  return layout;
}

}  // namespace apptraverse
