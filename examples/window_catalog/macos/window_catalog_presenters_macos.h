#ifndef APPTRAVERSE_MACOS_WINDOW_CATALOG_PRESENTERS_H_
#define APPTRAVERSE_MACOS_WINDOW_CATALOG_PRESENTERS_H_

#include "window_catalog_main_native_frame_wiring.h"
#include "window_catalog_list_presenter_mixin.h"
#include <cassert>
#include "apptraverse/platform/macos/macos_ui_presenters.h"

namespace apptraverse::example::surfaces {

class CatalogDesktopWindowPresenter
    : public WindowCatalogDesktopPresenterMixin<
          ui::macos::MacDesktopWindowPresenter> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::CatalogDesktopWindowPresenter",
      CatalogDesktopWindowPresenter, ui::macos::MacDesktopWindowPresenter, 0)

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

  bool PresentsChildControl(Presenter const& child) const override {
    // Native caption Close supplies this action; identify its content control
    // through the distilled prefab link, independently of model host mode.
    auto const& catalog_window = static_cast<CatalogWindow const&>(*window);
    return child.obj_id != catalog_window.close_button_presenter.id();
  }

  void OnLoad() override {
    assert(SessionMainNativeFrameQueue() != nullptr);
    assert(session_catalog_main_window_id_.is_valid());
    ui::macos::MacDesktopWindowPresenter::OnLoad();
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
    this->model_proxy->Invoke(
        catalog.id(), &WindowCatalog::SubmitHolderChildWindowFrameFromUi,
        this->window.id().id(), x, y, client_width, client_height,
        updates_restore_geometry);
  }
};

class CatalogListContainerPresenter
    : public WindowCatalogListPresenterMixin<
          ui::macos::MacListContainerPresenter> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::CatalogListContainerPresenter",
      CatalogListContainerPresenter, ui::macos::MacListContainerPresenter, 0)

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
