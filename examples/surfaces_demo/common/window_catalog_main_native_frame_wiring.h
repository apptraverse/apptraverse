#ifndef APPTRAVERSE_WINDOW_CATALOG_MAIN_NATIVE_FRAME_WIRING_H_
#define APPTRAVERSE_WINDOW_CATALOG_MAIN_NATIVE_FRAME_WIRING_H_

#include "aether-objects/obj/obj.h"

namespace apptraverse {

class WindowCatalogMainNativeFrameQueue;

void WireWindowCatalogMainNativeFrameQueue(
    ae::Obj& gui_root, WindowCatalogMainNativeFrameQueue& queue);
void UnwireWindowCatalogMainNativeFrameQueue(ae::Obj& gui_root);
[[nodiscard]] bool WindowCatalogMainNativeFrameQueueAttached(ae::Obj& gui_root);

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_CATALOG_MAIN_NATIVE_FRAME_WIRING_H_
