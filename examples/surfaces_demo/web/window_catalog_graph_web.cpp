#include "window_catalog_graph_web.h"

#include "window_catalog_graph_builder.h"
#include "window_catalog_model.h"

#include "apptraverse/object_macros.h"
#include "apptraverse/platform/web/web_ui_list_presenter.h"
#include "apptraverse/platform/web/web_ui_push_button_presenter.h"
#include "apptraverse/platform/web/web_ui_window_presenter.h"
#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse {

WindowCatalog::ptr BuildWebWindowCatalogGraph(ae::Domain& domain) {
  ui::web::EnsureWebUiPresenterRegistration();

  WindowCatalogPresenterFactory factory;
  factory.create_window = [](ae::Domain& build_domain, ae::ObjId presenter_id) {
    return ui::web::WebDesktopWindowPresenter::ptr::Create(
        ae::CreateWith{build_domain}.with_id(presenter_id));
  };
  factory.create_list =
      [](ae::Domain& build_domain, ae::ObjId presenter_id,
         WindowCatalog::ptr const& /*catalog*/) {
        return ui::web::WebListContainerPresenter::ptr::Create(
            ae::CreateWith{build_domain}.with_id(presenter_id));
      };
  factory.create_button = [](ae::Domain& build_domain) {
    return ui::web::WebPushButtonPresenter::ptr::Create(
        ae::CreateWith{build_domain});
  };

  return BuildWindowCatalogGraph(domain, factory);
}

void EnsureWebWindowCatalogGraphRegistration() {
  EnsureWindowCatalogModelRegistration();
  ui::EnsureUiPresenterRegistration();
  ui::web::EnsureWebUiPresenterRegistration();
}

}  // namespace apptraverse
