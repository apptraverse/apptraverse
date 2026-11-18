#include "window_catalog_graph.h"
#include <utility>
#include "window_catalog_graph_builder.h"
#include "window_catalog_menu.h"
#include "window_catalog_presenters_ios.h"

namespace apptraverse {

WindowCatalog::ptr BuildWindowCatalogGraph(ae::Domain& domain) {
  WindowCatalogPresenterFactory factory;
  factory.create_window = [](ae::Domain& build_domain, ae::ObjId presenter_id,
                             WindowCatalog::ptr const& catalog) {
    auto presenter = example::surfaces::IosCatalogWindowPresenter::ptr::Create(
        ae::CreateWith{build_domain}.with_id(presenter_id));
    presenter->catalog = catalog;
    return presenter;
  };
  factory.create_list = [](ae::Domain& build_domain, ae::ObjId presenter_id,
                           WindowCatalog::ptr const& catalog) {
    auto presenter = example::surfaces::IosCatalogListPresenter::ptr::Create(
        ae::CreateWith{build_domain}.with_id(presenter_id));
    presenter->catalog = catalog;
    return presenter;
  };
  factory.create_button = [](ae::Domain& build_domain, ae::ObjId presenter_id) {
    return ui::ios::IosPushButtonPresenter::ptr::Create(
        presenter_id.is_valid()
            ? ae::CreateWith{build_domain}.with_id(presenter_id)
            : ae::CreateWith{build_domain});
  };
  WindowCatalogGraphOptions options;
  options.mobile_close_in_prefab = true;
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
