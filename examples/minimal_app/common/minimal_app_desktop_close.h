#ifndef APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_DESKTOP_CLOSE_H_
#define APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_DESKTOP_CLOSE_H_

#include <functional>

#include "aether-objects/obj/obj.h"

namespace apptraverse {
class ModelObjectProxy;
}

namespace apptraverse::example::minimal {

void HandleMinimalAppDesktopWindowClose(
    ae::Obj& application_root, ae::Obj& closed_desktop_window,
    apptraverse::ModelObjectProxy& proxy,
    std::function<void()> request_application_stop);

}  // namespace apptraverse::example::minimal

#endif  // APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_DESKTOP_CLOSE_H_
