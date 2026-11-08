#ifndef APPTRAVERSE_WINDOW_CATALOG_MAIN_NATIVE_FRAME_WIRING_H_
#define APPTRAVERSE_WINDOW_CATALOG_MAIN_NATIVE_FRAME_WIRING_H_

#include "aether-objects/obj/obj.h"

namespace apptraverse {

class WindowCatalogMainNativeFrameQueue;

void WireWindowCatalogMainNativeFrameQueue(
    ae::Obj& gui_root, WindowCatalogMainNativeFrameQueue& queue);
void UnwireWindowCatalogMainNativeFrameQueue(ae::Obj& gui_root);
[[nodiscard]] bool WindowCatalogMainNativeFrameQueueAttached(ae::Obj& gui_root);
// Confirms prepare wired the main catalog desktop presenter: domain object
// matches live graph traversal and session queue pointer.
[[nodiscard]] bool WindowCatalogMainNativeFrameQueuePrepareBinding(
    ae::Obj& gui_root, ae::ObjId main_presenter_id,
    WindowCatalogMainNativeFrameQueue& queue, bool require_presentation_pending);

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_CATALOG_MAIN_NATIVE_FRAME_WIRING_H_
