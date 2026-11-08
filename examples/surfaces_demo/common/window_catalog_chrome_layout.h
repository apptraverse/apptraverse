#ifndef APPTRAVERSE_WINDOW_CATALOG_CHROME_LAYOUT_H_
#define APPTRAVERSE_WINDOW_CATALOG_CHROME_LAYOUT_H_

#include <cstdint>

namespace apptraverse {

struct WindowCatalogChromeRect {
  std::int32_t x{0};
  std::int32_t y{0};
  std::int32_t width{0};
  std::int32_t height{0};
};

// Layout of the main catalog window chrome in main-window client coordinates.
// Pure geometry: no platform types, no model access.
struct WindowCatalogMainChromeLayout {
  WindowCatalogChromeRect list;
  WindowCatalogChromeRect add;
  WindowCatalogChromeRect open;
  WindowCatalogChromeRect delete_button;
  WindowCatalogChromeRect language;
};

// Adaptive main chrome layout.
//  * Portrait (client_h > client_w): list on top, the four buttons stacked in
//    a full-width column at the bottom.
//  * Landscape: list on the left half, the four buttons stacked in a column on
//    the right half.
// Margins/button height follow the same relative geometry on all platforms in
// model client space. `density_dpi` is DisplayMetrics.densityDpi on Android so
// chrome matches ~12dp margins and >=48dp row height; on Web/Desktop pass 160.
[[nodiscard]] WindowCatalogMainChromeLayout ComputeWindowCatalogMainChromeLayout(
    std::int32_t client_w, std::int32_t client_h, std::int32_t density_dpi = 160);

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_CATALOG_CHROME_LAYOUT_H_
