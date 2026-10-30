#include "minimal_app_web_graph.h"

#include "apptraverse/platform/web/web_ui_list_presenter.h"
#include "apptraverse/platform/web/web_ui_push_button_presenter.h"
#include "apptraverse/platform/web/web_ui_window_presenter.h"
#include "apptraverse/runtime_node.h"

#include "minimal_app_graph_builder.h"

namespace apptraverse::example::minimal {

MinimalApplication::ptr BuildWebMinimalApplicationGraph(ae::Domain& domain) {
  ui::web::EnsureWebUiPresenterRegistration();
  MinimalPresenterFactories factories;
  factories.create_window = [](ae::Domain& d, ae::ObjId presenter_id) {
    return ui::web::WebDesktopWindowPresenter::ptr::Create(
        ae::CreateWith{d}.with_id(presenter_id));
  };
  factories.create_button = [](ae::Domain& d, ae::ObjId presenter_id) {
    return ui::web::WebPushButtonPresenter::ptr::Create(
        ae::CreateWith{d}.with_id(presenter_id));
  };
  factories.create_list = [](ae::Domain& d, ae::ObjId presenter_id) {
    return ui::web::WebListContainerPresenter::ptr::Create(
        ae::CreateWith{d}.with_id(presenter_id));
  };
  return BuildMinimalApplicationGraph(domain, factories);
}

}  // namespace apptraverse::example::minimal
