#ifndef APPTRAVERSE_WINDOW_CATALOG_WIN_PRESENTATION_HOST_H_
#define APPTRAVERSE_WINDOW_CATALOG_WIN_PRESENTATION_HOST_H_

#include "apptraverse/platform/windows/win_presentation_host.h"

namespace apptraverse {

class WindowCatalogMainNativeFrameQueue;

struct WindowCatalogWinPresentationHost : ui::windows::WinPresentationHost {
  WindowCatalogWinPresentationHost();
  explicit WindowCatalogWinPresentationHost(
      WindowCatalogMainNativeFrameQueue* main_native_frame_queue);

  WindowCatalogMainNativeFrameQueue* main_native_frame_queue;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_CATALOG_WIN_PRESENTATION_HOST_H_
