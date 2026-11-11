#ifndef APPTRAVERSE_LINUX_WINDOW_CATALOG_PRESENTERS_H_
#define APPTRAVERSE_LINUX_WINDOW_CATALOG_PRESENTERS_H_

#include "window_catalog_main_native_frame_wiring.h"
#include "window_catalog_list_presenter_mixin.h"
#include <cassert>
#include "apptraverse/platform/linux/linux_ui_list_presenter.h"
#include "apptraverse/platform/linux/linux_ui_window_presenter.h"

namespace apptraverse::example::surfaces {

class CatalogDesktopWindowPresenter
    : public WindowCatalogDesktopPresenterMixin<
          ui::linux::LinuxDesktopWindowPresenter> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::CatalogDesktopWindowPresenter",
      CatalogDesktopWindowPresenter, ui::linux::LinuxDesktopWindowPresenter, 0)

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

  void OnLoad() override {
    assert(SessionMainNativeFrameQueue() != nullptr);
    assert(session_catalog_main_window_id_.is_valid());
    ui::linux::LinuxDesktopWindowPresenter::OnLoad();
  }

  void ReportNativeFrameToModel(std::int32_t x, std::int32_t y,
                                std::int32_t client_width,
                                std::int32_t client_height,
                                bool updates_restore_geometry) override {
    ::apptraverse::WindowCatalogMainNativeFrameQueue* const queue =
        SessionMainNativeFrameQueue();
    if (this->window.id() == session_catalog_main_window_id_) {
      assert(queue != nullptr);
      assert(session_catalog_obj_id_.is_valid());
      ::apptraverse::PostWindowCatalogMainNativeFrame(
          *queue, this->model_proxy, session_catalog_obj_id_, this->window.id(),
          x, y, client_width, client_height, updates_restore_geometry);
      return;
    }
    ui::UiDesktopWindowPresenter::ReportNativeFrameToModel(
        x, y, client_width, client_height, updates_restore_geometry);
  }
};

class CatalogListContainerPresenter
    : public WindowCatalogListPresenterMixin<
          ui::linux::LinuxListContainerPresenter> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::CatalogListContainerPresenter",
      CatalogListContainerPresenter, ui::linux::LinuxListContainerPresenter, 0)

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
};


}  // namespace apptraverse::example::surfaces

#endif
