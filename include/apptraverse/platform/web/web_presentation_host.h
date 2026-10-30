#ifndef APPTRAVERSE_PLATFORM_WEB_WEB_PRESENTATION_HOST_H_
#define APPTRAVERSE_PLATFORM_WEB_WEB_PRESENTATION_HOST_H_

#include <emscripten/val.h>

namespace ae {
class Obj;
}

namespace apptraverse::ui::web {

// Passed as Presenter::presentation_host for Web UI. Not serialized.
// mount is set on the browser main thread before initial publication.
struct WebPresentationHost {
  emscripten::EM_VAL mount{nullptr};
  void (*on_desktop_window_user_close)(void* opaque, ae::Obj* desktop_window){
      nullptr};
  void* opaque{nullptr};
};

}  // namespace apptraverse::ui::web

#endif  // APPTRAVERSE_PLATFORM_WEB_WEB_PRESENTATION_HOST_H_
