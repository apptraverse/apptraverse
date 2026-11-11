#ifndef APPTRAVERSE_IOS_WINDOW_CATALOG_PRESENTERS_H_
#define APPTRAVERSE_IOS_WINDOW_CATALOG_PRESENTERS_H_

#include "window_catalog_list_presenter_mixin.h"
#include "apptraverse/platform/ios/ios_ui_presenters.h"

namespace apptraverse::example::surfaces {

class IosCatalogWindowPresenter
    : public WindowCatalogDesktopPresenterMixin<
          ui::ios::IosDesktopWindowPresenter> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::IosCatalogWindowPresenter",
      IosCatalogWindowPresenter, ui::ios::IosDesktopWindowPresenter, 0)
 protected:
  IosCatalogWindowPresenter() = default;
 public:
  explicit IosCatalogWindowPresenter(ae::ObjProp prop)
      : WindowCatalogDesktopPresenterMixin{prop} {}
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) { dnv(base_, catalog); }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const { dnv(base_, catalog); }
};

class IosCatalogListPresenter
    : public WindowCatalogListPresenterMixin<ui::ios::IosListContainerPresenter> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::IosCatalogListPresenter",
      IosCatalogListPresenter, ui::ios::IosListContainerPresenter, 0)
 protected:
  IosCatalogListPresenter() = default;
 public:
  explicit IosCatalogListPresenter(ae::ObjProp prop)
      : WindowCatalogListPresenterMixin{prop} {}
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) { dnv(base_, catalog); }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const { dnv(base_, catalog); }
  void SubmitListRowSelectionChangedFromUi(std::size_t row_index) override;
  void SubmitListRowActivatedFromUi(std::size_t row_index) override;
};

}  // namespace apptraverse::example::surfaces
#endif
