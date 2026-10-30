#ifndef APPTRAVERSE_PLATFORM_WEB_WEB_PRESENTATION_HOST_H_
#define APPTRAVERSE_PLATFORM_WEB_WEB_PRESENTATION_HOST_H_

#include <emscripten/val.h>

namespace apptraverse::ui::web {

// Passed as Presenter::presentation_host for Web UI. Not serialized.
// mount is set on the browser main thread before initial publication.
struct WebPresentationHost {
  emscripten::EM_VAL mount{nullptr};
};

}  // namespace apptraverse::ui::web

#endif  // APPTRAVERSE_PLATFORM_WEB_WEB_PRESENTATION_HOST_H_
