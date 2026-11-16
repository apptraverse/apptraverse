#ifndef APPTRAVERSE_WINDOW_CATALOG_PRESENTERS_ANDROID_H_
#define APPTRAVERSE_WINDOW_CATALOG_PRESENTERS_ANDROID_H_

#include "window_catalog_list_presenter_mixin.h"
#include "apptraverse/platform/android/android_ui_window_presenter.h"
#include "apptraverse/platform/android/android_ui_list_presenter.h"

namespace apptraverse::example::surfaces {
class CatalogDesktopWindowPresenter
    : public WindowCatalogDesktopPresenterMixin<
          ui::android::AndroidDesktopWindowPresenter> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::CatalogDesktopWindowPresenter",
      CatalogDesktopWindowPresenter,
      ui::android::AndroidDesktopWindowPresenter, 0)

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

class AndroidCatalogListContainerPresenter
    : public WindowCatalogListPresenterMixin<
          ui::android::AndroidListContainerPresenter> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::CatalogListContainerPresenter",
      AndroidCatalogListContainerPresenter,
      ui::android::AndroidListContainerPresenter, 0)

 protected:
  AndroidCatalogListContainerPresenter() = default;

 public:
  explicit AndroidCatalogListContainerPresenter(ae::ObjProp prop)
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

}  // namespace apptraverse::example::surfaces
#endif
