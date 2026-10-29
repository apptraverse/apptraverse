#ifndef APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_GRAPH_BUILDER_H_
#define APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_GRAPH_BUILDER_H_

#include <functional>

#include "aether-objects/obj/domain.h"

#include "apptraverse/ui/ui_presenters.h"

#include "minimal_application.h"

namespace apptraverse::example::minimal {

using CreateMinimalWindowPresenterFn =
    std::function<ui::UiDesktopWindowPresenter::ptr(ae::Domain&, ae::ObjId)>;

MinimalApplication::ptr BuildMinimalApplicationGraph(
    ae::Domain& domain, CreateMinimalWindowPresenterFn create_window);

}  // namespace apptraverse::example::minimal

#endif  // APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_GRAPH_BUILDER_H_
