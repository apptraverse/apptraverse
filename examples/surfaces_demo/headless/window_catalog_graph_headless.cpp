#include "window_catalog_graph_headless.h"

#include "window_catalog_graph_builder.h"
#include "window_catalog_list_presenter_mixin.h"
#include "window_catalog_model.h"

#include "apptraverse/model_object_proxy.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::example::surfaces {

class HeadlessCatalogListContainerPresenter
    : public WindowCatalogListPresenterMixin<ui::UiListContainerPresenter> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::HeadlessCatalogListContainerPresenter",
      HeadlessCatalogListContainerPresenter, ui::UiListContainerPresenter, 0)

 protected:
  HeadlessCatalogListContainerPresenter() = default;

 public:
  explicit HeadlessCatalogListContainerPresenter(ae::ObjProp prop)
      : WindowCatalogListPresenterMixin{prop} {}

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, catalog);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, catalog);
  }

  void SubmitListRowSelectionChangedFromUi(std::size_t row_index) override;
  void SubmitListRowActivatedFromUi(std::size_t row_index) override;
};

void HeadlessCatalogListContainerPresenter::SubmitListRowSelectionChangedFromUi(
    std::size_t row_index) {
  auto const row_id = RowIdAtUiIndex(row_index);
  if (!row_id) { return; }
  model_proxy->Invoke(catalog->obj_id, &WindowCatalog::SubmitSelectionByIdFromUi,
                      *row_id);
}

void HeadlessCatalogListContainerPresenter::SubmitListRowActivatedFromUi(
    std::size_t row_index) {
  auto const row_id = RowIdAtUiIndex(row_index);
  if (!row_id) { return; }
  model_proxy->Invoke(catalog->obj_id, &WindowCatalog::OpenHolderByIdFromUi, *row_id);
}

APPTRAVERSE_REGISTER(HeadlessCatalogListContainerPresenter);

}  // namespace apptraverse::example::surfaces

namespace apptraverse {

WindowCatalog::ptr BuildHeadlessWindowCatalogGraph(ae::Domain& domain) {
  return BuildHeadlessWindowCatalogGraph(domain, WindowCatalogGraphOptions{});
}

WindowCatalog::ptr BuildHeadlessWindowCatalogGraph(
    ae::Domain& domain, WindowCatalogGraphOptions options) {
  ui::EnsureUiPresenterRegistration();

  WindowCatalogPresenterFactory factory;
  factory.create_window = [](ae::Domain& build_domain, ae::ObjId presenter_id,
                             WindowCatalog::ptr const&) {
    return ui::UiDesktopWindowPresenter::ptr::Create(
        ae::CreateWith{build_domain}.with_id(presenter_id));
  };
  factory.create_list =
      [](ae::Domain& build_domain, ae::ObjId presenter_id,
         WindowCatalog::ptr const& catalog) {
        example::surfaces::HeadlessCatalogListContainerPresenter::ptr presenter =
            example::surfaces::HeadlessCatalogListContainerPresenter::ptr::
                Create(ae::CreateWith{build_domain}.with_id(presenter_id));
        presenter->catalog = catalog;
        return presenter;
      };
  factory.create_button = [](ae::Domain& build_domain, ae::ObjId presenter_id) {
    return ui::UiPushButtonPresenter::ptr::Create(presenter_id.is_valid()
            ? ae::CreateWith{build_domain}.with_id(presenter_id)
            : ae::CreateWith{build_domain});
  };

  return BuildWindowCatalogGraph(domain, factory, options);
}

void EnsureHeadlessWindowCatalogGraphRegistration() {
  EnsureWindowCatalogModelRegistration();
  ui::EnsureUiPresenterRegistration();
  (void)&example::surfaces::
      g_apptraverse_registrar_HeadlessCatalogListContainerPresenter;
}

}  // namespace apptraverse
