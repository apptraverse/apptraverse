#ifndef APPTRAVERSE_WINDOW_CATALOG_PREFAB_SIZES_H_
#define APPTRAVERSE_WINDOW_CATALOG_PREFAB_SIZES_H_

#include <cstdint>

namespace apptraverse {

// Mobile window prefab: must stay consistent across graph_builder and
// graph_mobile_prefab (Close button placement).
inline constexpr std::int32_t kWindowCatalogPrefabClientWidth = 480;
inline constexpr std::int32_t kWindowCatalogPrefabClientHeight = 320;

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_CATALOG_PREFAB_SIZES_H_
