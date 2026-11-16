#ifndef APPTRAVERSE_WINDOW_CATALOG_PRESENTERS_WEB_H_
#define APPTRAVERSE_WINDOW_CATALOG_PRESENTERS_WEB_H_

#include "window_catalog_list_presenter_mixin.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/platform/web/web_ui_list_presenter.h"
#include "apptraverse/platform/web/web_ui_window_presenter.h"

namespace apptraverse::example::surfaces {

class CatalogDesktopWindowPresenter
    : public WindowCatalogDesktopPresenterMixin<ui::web::WebDesktopWindowPresenter> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::CatalogDesktopWindowPresenter",
      CatalogDesktopWindowPresenter, ui::web::WebDesktopWindowPresenter, 0)
 protected:
  CatalogDesktopWindowPresenter() = default;
 public:
  explicit CatalogDesktopWindowPresenter(ae::ObjProp prop)
      : WindowCatalogDesktopPresenterMixin{prop} {}
  void OnLoad() override;
  template <typename Dnv> void Load(ae::Version<0>, Dnv& dnv) { dnv(base_, catalog); }
  template <typename Dnv> void Save(ae::Version<0>, Dnv& dnv) const { dnv(base_, catalog); }
};

class WebCatalogListContainerPresenter
    : public WindowCatalogListPresenterMixin<ui::web::WebListContainerPresenter> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::CatalogListContainerPresenter",
      WebCatalogListContainerPresenter, ui::web::WebListContainerPresenter, 0)
 protected:
  WebCatalogListContainerPresenter() = default;
 public:
  explicit WebCatalogListContainerPresenter(ae::ObjProp prop)
      : WindowCatalogListPresenterMixin{prop} {}
  template <typename Dnv> void Load(ae::Version<0>, Dnv& dnv) { dnv(base_, catalog); }
  template <typename Dnv> void Save(ae::Version<0>, Dnv& dnv) const { dnv(base_, catalog); }
  void SubmitListRowSelectionChangedFromUi(std::size_t row_index) override;
  void SubmitListRowActivatedFromUi(std::size_t row_index) override;
};
}  // namespace apptraverse::example::surfaces
#endif
