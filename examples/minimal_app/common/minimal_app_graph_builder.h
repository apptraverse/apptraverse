#ifndef APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_GRAPH_BUILDER_H_
#define APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_GRAPH_BUILDER_H_

#include <functional>

#include "aether-objects/obj/domain.h"

#include "apptraverse/ui/ui_presenters.h"
#include "apptraverse/ui/ui_push_button_presenter.h"

#include "minimal_application.h"

namespace apptraverse::example::minimal {

using CreateMinimalWindowPresenterFn =
    std::function<ui::UiDesktopWindowPresenter::ptr(ae::Domain&, ae::ObjId)>;

using CreateMinimalPushButtonPresenterFn =
    std::function<ui::UiPushButtonPresenter::ptr(ae::Domain&, ae::ObjId)>;

using CreateMinimalListPresenterFn =
    std::function<ui::UiListContainerPresenter::ptr(ae::Domain&, ae::ObjId)>;

struct MinimalPresenterFactories {
  CreateMinimalWindowPresenterFn create_window;
  CreateMinimalPushButtonPresenterFn create_button;
  CreateMinimalListPresenterFn create_list;
};

MinimalApplication::ptr BuildMinimalApplicationGraph(
    ae::Domain& domain, MinimalPresenterFactories const& factories);

}  // namespace apptraverse::example::minimal

#endif  // APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_GRAPH_BUILDER_H_
