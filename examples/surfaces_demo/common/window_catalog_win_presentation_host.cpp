#include "window_catalog_win_presentation_host.h"

#include "window_catalog_main_native_frame_queue.h"

namespace apptraverse {

WindowCatalogWinPresentationHost::WindowCatalogWinPresentationHost()
    : main_native_frame_queue(nullptr) {}

WindowCatalogWinPresentationHost::WindowCatalogWinPresentationHost(
    WindowCatalogMainNativeFrameQueue* main_native_frame_queue)
    : main_native_frame_queue(main_native_frame_queue) {}

}  // namespace apptraverse
