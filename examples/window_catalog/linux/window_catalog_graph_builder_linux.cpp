#include "window_catalog_graph.h"
#include "window_catalog_graph_builder.h"
#include "window_catalog_menu.h"
#include "window_catalog_presenters_linux.h"
#include "apptraverse/platform/linux/linux_ui_push_button_presenter.h"

namespace apptraverse {
WindowCatalog::ptr BuildWindowCatalogGraph(ae::Domain& domain) {
  WindowCatalogPresenterFactory factory;
  factory.create_window =
      [](ae::Domain& build_domain, ae::ObjId presenter_id,
         WindowCatalog::ptr const& catalog) {
        example::surfaces::CatalogDesktopWindowPresenter::ptr presenter =
            example::surfaces::CatalogDesktopWindowPresenter::ptr::Create(
                ae::CreateWith{build_domain}.with_id(presenter_id));
        presenter->catalog = catalog;
        return presenter;
      };
  factory.create_list =
      [](ae::Domain& build_domain, ae::ObjId presenter_id,
         WindowCatalog::ptr const& catalog) {
        example::surfaces::CatalogListContainerPresenter::ptr presenter =
            example::surfaces::CatalogListContainerPresenter::ptr::Create(
                ae::CreateWith{build_domain}.with_id(presenter_id));
        presenter->catalog = catalog;
        return presenter;
      };
  factory.create_button = [](ae::Domain& build_domain, ae::ObjId presenter_id) {
    return ui::linux::LinuxPushButtonPresenter::ptr::Create(
        presenter_id.is_valid()
            ? ae::CreateWith{build_domain}.with_id(presenter_id)
            : ae::CreateWith{build_domain});
  };

  WindowCatalogGraphOptions options;
  options.create_device = [](ae::Domain& build_domain, ae::ObjId id) {
    auto state = CatalogUiState::ptr::Create(ae::CreateWith{build_domain}.with_id(id));
    state->catalog = {}; state->menu = {};
    return ui::UiDeviceState::ptr{state};
  };
  options.language_button = false;
  auto catalog = BuildWindowCatalogGraph(domain, factory, std::move(options));
  ComposeCatalogMenu(domain, *catalog, CatalogThemeHandling::kAdapterAppliesTheme);
  return catalog;
}

}  // namespace apptraverse
