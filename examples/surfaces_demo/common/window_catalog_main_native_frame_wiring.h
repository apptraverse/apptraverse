#ifndef APPTRAVERSE_WINDOW_CATALOG_MAIN_NATIVE_FRAME_WIRING_H_
#define APPTRAVERSE_WINDOW_CATALOG_MAIN_NATIVE_FRAME_WIRING_H_

#include "aether-objects/obj/obj.h"

#if defined(_WIN32)
struct HWND__;
using HWND = HWND__*;
#endif

namespace apptraverse {

class WindowCatalogMainNativeFrameQueue;

void WireWindowCatalogMainNativeFrameQueue(
    ae::Obj& gui_root, WindowCatalogMainNativeFrameQueue& queue);
void UnwireWindowCatalogMainNativeFrameQueue(ae::Obj& gui_root);
[[nodiscard]] bool WindowCatalogMainNativeFrameQueueAttached(ae::Obj& gui_root);
#if defined(_WIN32)
void WireWindowCatalogMainNativeFrameQueueForHwnd(
    HWND hwnd, WindowCatalogMainNativeFrameQueue& queue);
#endif

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_CATALOG_MAIN_NATIVE_FRAME_WIRING_H_
