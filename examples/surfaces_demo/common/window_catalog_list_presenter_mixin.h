#ifndef APPTRAVERSE_WINDOW_CATALOG_LIST_PRESENTER_MIXIN_H_
#define APPTRAVERSE_WINDOW_CATALOG_LIST_PRESENTER_MIXIN_H_

#include "window_catalog_list_row_text.h"
#include "window_catalog_main_native_frame_queue.h"
#include "window_catalog_model.h"

#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::example::surfaces {

template <typename BaseListPresenter>
class WindowCatalogListPresenterMixin : public BaseListPresenter {
 public:
  using BaseListPresenter::BaseListPresenter;

  WindowCatalog::ptr catalog;

  std::string ResolveListRowPrimaryText(ui::UiListRow::ptr const& row,
                                       int language_index) const override {
    catalog.Load();
    return ResolveWindowCatalogListRowPrimaryText(row, *catalog, language_index);
  }

  std::vector<std::string> ResolveListRowPrimaryTexts(
      int language_index) const override {
    this->container.Load();
    catalog.Load();
    return ResolveWindowCatalogListRowPrimaryTexts(
        *this->container, *catalog, language_index);
  }
};

template <typename BaseDesktopPresenter>
class WindowCatalogDesktopPresenterMixin : public BaseDesktopPresenter {
 public:
  using BaseDesktopPresenter::BaseDesktopPresenter;

  WindowCatalogDesktopPresenterMixin()
      : session_main_native_frame_queue_(nullptr),
        session_catalog_obj_id_(),
        session_catalog_main_window_id_() {}

  explicit WindowCatalogDesktopPresenterMixin(ae::ObjProp prop)
      : BaseDesktopPresenter{prop},
        session_main_native_frame_queue_(nullptr),
        session_catalog_obj_id_(),
        session_catalog_main_window_id_() {}

  WindowCatalog::ptr catalog;

  void AttachSessionMainNativeFrameQueue(
      ::apptraverse::WindowCatalogMainNativeFrameQueue* queue,
      ae::ObjId catalog_obj_id, ae::ObjId catalog_main_window_id) {
    session_main_native_frame_queue_ = queue;
    session_catalog_obj_id_ = catalog_obj_id;
    session_catalog_main_window_id_ = catalog_main_window_id;
  }

  void DetachSessionMainNativeFrameQueue() {
    session_main_native_frame_queue_ = nullptr;
    session_catalog_obj_id_ = {};
    session_catalog_main_window_id_ = {};
  }

  [[nodiscard]] ::apptraverse::WindowCatalogMainNativeFrameQueue*
  SessionMainNativeFrameQueue() const {
    return session_main_native_frame_queue_;
  }

 protected:
  ::apptraverse::WindowCatalogMainNativeFrameQueue*
      session_main_native_frame_queue_;
  ae::ObjId session_catalog_obj_id_;
  ae::ObjId session_catalog_main_window_id_;
  void SubmitMainCatalogViewportFromNative(std::int32_t width,
                                           std::int32_t height) {
    if (this->model_proxy == nullptr) {
      return;
    }
    catalog.Load();
    if (this->window->obj_id != catalog->persisted.main_window.id()) {
      return;
    }
    if (width <= 0 || height <= 0) {
      return;
    }
    this->model_proxy->Invoke(catalog->obj_id,
                              &WindowCatalog::SubmitMainClientViewportFromUi,
                              width, height, 160);
  }

  std::string ResolvePresentedWindowTitle(int language_index) const override {
    catalog.Load();
    if (this->window->obj_id == catalog->persisted.main_window.id()) {
      return BaseDesktopPresenter::ResolvePresentedWindowTitle(language_index);
    }
    return ResolveWindowCatalogChildWindowTitle(
        *catalog, this->window->obj_id, language_index);
  }
};

}  // namespace apptraverse::example::surfaces

#endif  // APPTRAVERSE_WINDOW_CATALOG_LIST_PRESENTER_MIXIN_H_
