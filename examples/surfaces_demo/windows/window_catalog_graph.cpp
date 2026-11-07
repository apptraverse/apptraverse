#include "window_catalog_graph.h"

#include "window_catalog_actions.h"
#include "window_catalog_graph_builder.h"
#include "window_catalog_ids.h"
#include "window_catalog_list_presenter_mixin.h"
#include "window_catalog_model.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/platform/windows/win_ui_push_button_presenter.h"
#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::example::surfaces {

class CatalogDesktopWindowPresenter
    : public WindowCatalogDesktopPresenterMixin<
          ui::windows::WinDesktopWindowPresenter> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::CatalogDesktopWindowPresenter",
      CatalogDesktopWindowPresenter, ui::windows::WinDesktopWindowPresenter, 0)

 protected:
  CatalogDesktopWindowPresenter() = default;

 public:
  explicit CatalogDesktopWindowPresenter(ae::ObjProp prop)
      : WindowCatalogDesktopPresenterMixin{prop} {}

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, catalog);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, catalog);
  }
};

class CatalogListContainerPresenter
    : public WindowCatalogListPresenterMixin<
          ui::windows::WinListContainerPresenter> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::CatalogListContainerPresenter",
      CatalogListContainerPresenter, ui::windows::WinListContainerPresenter, 0)

 protected:
  CatalogListContainerPresenter() = default;

 public:
  explicit CatalogListContainerPresenter(ae::ObjProp prop)
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
  void OnModelChanged() override;
};

void CatalogListContainerPresenter::SubmitListRowSelectionChangedFromUi(
    std::size_t row_index) {
  model_proxy->Invoke(catalog->obj_id, &WindowCatalog::SubmitSelectionFromUi,
                      row_index);
}

void CatalogListContainerPresenter::SubmitListRowActivatedFromUi(
    std::size_t row_index) {
  model_proxy->Invoke(catalog->obj_id, &WindowCatalog::OpenRow, row_index);
}

// Main client size is reported to the model by the desktop window presenter
// (WM_SIZE / client mount size-allocate); mirror it into the catalog viewport
// so chrome layout adapts.
void CatalogListContainerPresenter::OnModelChanged() {
  WinListContainerPresenter::OnModelChanged();
  if (catalog.is_loaded()) {
    ReportCatalogMainViewportFromMirror(*catalog, model_proxy);
  }
}

APPTRAVERSE_REGISTER(CatalogDesktopWindowPresenter);
APPTRAVERSE_REGISTER(CatalogListContainerPresenter);

}  // namespace apptraverse::example::surfaces

namespace apptraverse {

WindowCatalog::ptr BuildWindowCatalogGraph(ae::Domain& domain) {
  ui::EnsureUiPresenterRegistration();
  ui::windows::EnsureWinUiPresenterRegistration();

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

  return BuildWindowCatalogGraph(domain, factory);
}

void EnsureWindowCatalogGraphRegistration() {
  EnsureWindowCatalogModelRegistration();
  ui::EnsureUiPresenterRegistration();
  ui::windows::EnsureWinUiPresenterRegistration();
  (void)&example::surfaces::g_apptraverse_registrar_CatalogListContainerPresenter;
  (void)&example::surfaces::g_apptraverse_registrar_CatalogDesktopWindowPresenter;
}

}  // namespace apptraverse

