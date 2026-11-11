#include "window_catalog_graph_web.h"
#include "window_catalog_graph_builder.h"
#include "window_catalog_presenters_web.h"
#include "window_catalog_ids.h"
#include "apptraverse/platform/web/web_ui_push_button_presenter.h"

namespace apptraverse {
WindowCatalog::ptr BuildWebWindowCatalogGraph(ae::Domain& domain) {
  WindowCatalogPresenterFactory factory;
  factory.create_window = [](ae::Domain& build_domain, ae::ObjId presenter_id,
                             WindowCatalog::ptr const& catalog)
      -> ui::UiDesktopWindowPresenter::ptr {
    if (presenter_id.id() ==
        window_catalog_demo::ToObjId(
            window_catalog_demo::ObjId::MainWindowPresenter)) {
      return example::surfaces::WebCatalogWindowPresenter::ptr::Create(
          ae::CreateWith{build_domain}.with_id(presenter_id));
    }
    example::surfaces::CatalogDesktopWindowPresenter::ptr presenter =
        example::surfaces::CatalogDesktopWindowPresenter::ptr::Create(
            ae::CreateWith{build_domain}.with_id(presenter_id));
    presenter->catalog = catalog;
    return presenter;
  };
  factory.create_list =
      [](ae::Domain& build_domain, ae::ObjId presenter_id,
         WindowCatalog::ptr const& catalog) {
        example::surfaces::WebCatalogListContainerPresenter::ptr presenter =
            example::surfaces::WebCatalogListContainerPresenter::ptr::Create(
                ae::CreateWith{build_domain}.with_id(presenter_id));
        presenter->catalog = catalog;
        return presenter;
      };
  factory.create_button = [](ae::Domain& build_domain, ae::ObjId presenter_id) {
    return ui::web::WebPushButtonPresenter::ptr::Create(
        presenter_id.is_valid()
            ? ae::CreateWith{build_domain}.with_id(presenter_id)
            : ae::CreateWith{build_domain});
  };

  // Same mobile pager model as Android: main page + one page per open child
  // window, Close button in the window prefab graph.
  WindowCatalogGraphOptions options;
  options.mobile_pager = true;
  options.mobile_close_in_prefab = true;
  return BuildWindowCatalogGraph(domain, factory, options);
}

}  // namespace apptraverse
