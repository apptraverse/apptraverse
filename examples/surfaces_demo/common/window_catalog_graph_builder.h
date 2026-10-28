#ifndef APPTRAVERSE_WINDOW_CATALOG_GRAPH_BUILDER_H_
#define APPTRAVERSE_WINDOW_CATALOG_GRAPH_BUILDER_H_

#include <functional>

#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj_id.h"

#include "window_catalog_model.h"

#include "apptraverse/ui/ui_presenters.h"
#include "apptraverse/ui/ui_push_button_presenter.h"

namespace apptraverse {

struct WindowCatalogPresenterFactory {
  std::function<ui::UiDesktopWindowPresenter::ptr(ae::Domain&, ae::ObjId)>
      create_window;
  std::function<ui::UiListContainerPresenter::ptr(
      ae::Domain&, ae::ObjId, WindowCatalog::ptr const&)>
      create_list;
  std::function<ui::UiPushButtonPresenter::ptr(ae::Domain&)> create_button;
};

WindowCatalog::ptr BuildWindowCatalogGraph(
    ae::Domain& domain, WindowCatalogPresenterFactory const& factory);

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_CATALOG_GRAPH_BUILDER_H_
