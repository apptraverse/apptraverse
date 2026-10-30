#include "minimal_app_web_graph.h"

#include "apptraverse/platform/web/web_ui_window_presenter.h"
#include "apptraverse/runtime_node.h"

#include "minimal_app_graph_builder.h"

namespace apptraverse::example::minimal {

MinimalApplication::ptr BuildWebMinimalApplicationGraph(ae::Domain& domain) {
  ui::web::EnsureWebUiPresenterRegistration();
  return BuildMinimalApplicationGraph(
      domain, [](ae::Domain& d, ae::ObjId presenter_id) {
        return ui::web::WebDesktopWindowPresenter::ptr::Create(
            ae::CreateWith{d}.with_id(presenter_id));
      });
}

}  // namespace apptraverse::example::minimal
