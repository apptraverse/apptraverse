#include "window_catalog_graph_headless.h"

#include "window_catalog_graph_builder.h"
#include "window_catalog_model.h"

#include "apptraverse/object_macros.h"
#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse {

WindowCatalog::ptr BuildHeadlessWindowCatalogGraph(ae::Domain& domain) {
  ui::EnsureUiPresenterRegistration();

  WindowCatalogPresenterFactory factory;
  factory.create_window = [](ae::Domain& build_domain, ae::ObjId presenter_id) {
    return ui::UiDesktopWindowPresenter::ptr::Create(
        ae::CreateWith{build_domain}.with_id(presenter_id));
  };
  factory.create_list =
      [](ae::Domain& build_domain, ae::ObjId presenter_id,
         WindowCatalog::ptr const& /*catalog*/) {
        return ui::UiListContainerPresenter::ptr::Create(
            ae::CreateWith{build_domain}.with_id(presenter_id));
      };
  factory.create_button = [](ae::Domain& build_domain) {
    return ui::UiPushButtonPresenter::ptr::Create(ae::CreateWith{build_domain});
  };

  return BuildWindowCatalogGraph(domain, factory);
}

void EnsureHeadlessWindowCatalogGraphRegistration() {
  EnsureWindowCatalogModelRegistration();
  ui::EnsureUiPresenterRegistration();
}

}  // namespace apptraverse
