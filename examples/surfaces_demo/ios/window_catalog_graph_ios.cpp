#include "window_catalog_platform_graph.h"

#include "window_catalog_graph_builder.h"

#include "apptraverse/platform/ios/ios_ui_presenters.h"
#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse {

WindowCatalog::ptr BuildWindowCatalogGraph(ae::Domain& domain) {
  ui::ios::EnsureIosUiPresenterRegistration();

  WindowCatalogPresenterFactory factory;
  factory.create_window = [](ae::Domain& build_domain, ae::ObjId presenter_id) {
    return ui::ios::IosDesktopWindowPresenter::ptr::Create(
        ae::CreateWith{build_domain}.with_id(presenter_id));
  };
  factory.create_list =
      [](ae::Domain& build_domain, ae::ObjId presenter_id,
         WindowCatalog::ptr const&) {
        return ui::ios::IosListContainerPresenter::ptr::Create(
            ae::CreateWith{build_domain}.with_id(presenter_id));
      };
  factory.create_button = [](ae::Domain& build_domain) {
    return ui::ios::IosPushButtonPresenter::ptr::Create(
        ae::CreateWith{build_domain});
  };
  return BuildWindowCatalogGraph(domain, factory);
}

void EnsureWindowCatalogGraphRegistration() {
  EnsureWindowCatalogModelRegistration();
  ui::ios::EnsureIosUiPresenterRegistration();
}

}  // namespace apptraverse
