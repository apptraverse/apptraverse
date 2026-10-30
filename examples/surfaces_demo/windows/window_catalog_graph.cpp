#include "window_catalog_graph.h"

#include "window_catalog_graph_builder.h"
#include "window_catalog_model.h"

#include "apptraverse/object_macros.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/platform/windows/win_ui_push_button_presenter.h"
#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse {

WindowCatalog::ptr BuildWindowCatalogGraph(ae::Domain& domain) {
  ui::EnsureUiPresenterRegistration();
  ui::windows::EnsureWinUiPresenterRegistration();

  WindowCatalogPresenterFactory factory;
  factory.create_window =
      [](ae::Domain& build_domain, ae::ObjId presenter_id) {
        return ui::windows::WinDesktopWindowPresenter::ptr::Create(
            ae::CreateWith{build_domain}.with_id(presenter_id));
      };
  factory.create_list =
      [](ae::Domain& build_domain, ae::ObjId presenter_id,
         WindowCatalog::ptr const& /*catalog*/) {
        return ui::windows::WinListContainerPresenter::ptr::Create(
            ae::CreateWith{build_domain}.with_id(presenter_id));
      };
  factory.create_button = [](ae::Domain& build_domain) {
    return ui::windows::WinPushButtonPresenter::ptr::Create(
        ae::CreateWith{build_domain});
  };

  return BuildWindowCatalogGraph(domain, factory);
}

void EnsureWindowCatalogGraphRegistration() {
  EnsureWindowCatalogModelRegistration();
  ui::EnsureUiPresenterRegistration();
  ui::windows::EnsureWinUiPresenterRegistration();
}

}  // namespace apptraverse
