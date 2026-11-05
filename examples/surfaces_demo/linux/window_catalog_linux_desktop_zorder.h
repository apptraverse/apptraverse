#ifndef APPTRAVERSE_EXAMPLE_SURFACES_LINUX_WINDOW_CATALOG_LINUX_DESKTOP_ZORDER_H_
#define APPTRAVERSE_EXAMPLE_SURFACES_LINUX_WINDOW_CATALOG_LINUX_DESKTOP_ZORDER_H_

#include <cstdint>
#include <vector>

#include "window_catalog_model.h"

namespace apptraverse::example::surfaces {

std::vector<std::uint32_t> CaptureCatalogDesktopZOrderTopToBottom(
    WindowCatalog& catalog);

void RestoreCatalogDesktopZOrder(WindowCatalog& catalog);

void FlushCatalogNativeFrames(WindowCatalog& catalog);

}  // namespace apptraverse::example::surfaces

#endif  // APPTRAVERSE_EXAMPLE_SURFACES_LINUX_WINDOW_CATALOG_LINUX_DESKTOP_ZORDER_H_
