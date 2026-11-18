#include "window_catalog_graph.h"
#include "window_catalog_graph_builder.h"
#include "window_catalog_presenters.h"
#include "window_catalog_menu.h"

namespace apptraverse {
WindowCatalog::ptr BuildWindowCatalogGraph(ae::Domain& domain) {
  return BuildWindowCatalogGraph(domain, CatalogThemeHandling::kAdapterAppliesTheme);
}
WindowCatalog::ptr BuildWindowCatalogGraph(ae::Domain& domain, CatalogThemeHandling handling) {
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
    return ui::windows::WinPushButtonPresenter::ptr::Create(
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
  // Author the menu-only Windows layout once, before distillation.
  auto layout = DefaultWindowCatalogMainLayout();
  std::erase_if(layout.children.at(1).children,
      [](auto const& node) { return node.control_id == 5005; });
  options.main_layout = std::move(layout);
  auto catalog = BuildWindowCatalogGraph(domain, factory, std::move(options));
  ComposeCatalogMenu(domain, *catalog, handling);
  return catalog;
}

}  // namespace apptraverse
