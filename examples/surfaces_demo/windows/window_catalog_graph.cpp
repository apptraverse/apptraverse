#include "window_catalog_graph.h"

#include "window_catalog_actions.h"
#include "window_catalog_main_native_frame_queue.h"
#include "window_catalog_main_native_frame_wiring.h"
#include "window_catalog_graph_builder.h"
#include "window_catalog_ids.h"
#include "window_catalog_list_presenter_mixin.h"
#include "window_catalog_model.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include <windows.h>
#include "apptraverse/platform/windows/win_ui_push_button_presenter.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/presenter.h"
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

  void ReportNativeFrameToModel(std::int32_t x, std::int32_t y,
                                std::int32_t client_width,
                                std::int32_t client_height,
                                bool updates_restore_geometry) override {
    if (this->model_proxy == nullptr) {
      return;
    }
    catalog.Load();
    if (this->window->obj_id != catalog->persisted.main_window.id()) {
      assert(this->window.is_valid());
      ui::UiDesktopWindowPresenter::ReportNativeFrameToModel(
          x, y, client_width, client_height, updates_restore_geometry);
      return;
    }
    assert(catalog.is_valid() && this->window.is_valid());
    WindowCatalogMainNativeFrameQueue* const queue =
        SessionMainNativeFrameQueue();
    assert(queue != nullptr);
    ::apptraverse::PostWindowCatalogMainNativeFrame(
        *queue, this->model_proxy, catalog->obj_id,
                                     this->window->obj_id, x, y, client_width,
                                     client_height, updates_restore_geometry);
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

namespace {

void ForEachCatalogDesktopPresenter(
    ae::Obj& gui_root,
    void (*fn)(example::surfaces::CatalogDesktopWindowPresenter&, void*),
    void* arg) {
  std::vector<ae::Obj*> objects;
  CollectLiveReachableObjects(gui_root, objects);
  auto& registry = ae::Registry::GetRegistry();
  for (ae::Obj* obj : objects) {
    if (registry.GenerationDistance(
            example::surfaces::CatalogDesktopWindowPresenter::kClassId,
            obj->GetClassId()) < 0) {
      continue;
    }
    auto* catalog_presenter =
        static_cast<example::surfaces::CatalogDesktopWindowPresenter*>(obj);
    fn(*catalog_presenter, arg);
  }
}

void AttachQueue(example::surfaces::CatalogDesktopWindowPresenter& presenter,
                 void* arg) {
  auto* queue = static_cast<WindowCatalogMainNativeFrameQueue*>(arg);
  presenter.AttachSessionMainNativeFrameQueue(queue);
}

void DetachQueue(example::surfaces::CatalogDesktopWindowPresenter& presenter,
                 void*) {
  presenter.DetachSessionMainNativeFrameQueue();
}

}  // namespace

void WireWindowCatalogMainNativeFrameQueue(
    ae::Obj& gui_root, WindowCatalogMainNativeFrameQueue& queue) {
  ForEachCatalogDesktopPresenter(gui_root, AttachQueue, &queue);
  auto& catalog = static_cast<WindowCatalog&>(gui_root);
  catalog.persisted.main_window.Load();
  catalog.persisted.main_window->presenter.Load();
  if (!catalog.persisted.main_window->presenter) {
    return;
  }
  ui::UiWindowPresenter* const ui_presenter =
      catalog.persisted.main_window->presenter.operator->();
  auto& registry = ae::Registry::GetRegistry();
  if (registry.GenerationDistance(
          ui::windows::WinDesktopWindowPresenter::kClassId,
          ui_presenter->GetClassId()) < 0) {
    return;
  }
  auto* win_presenter =
      static_cast<ui::windows::WinDesktopWindowPresenter*>(ui_presenter);
  static_cast<example::surfaces::CatalogDesktopWindowPresenter*>(win_presenter)
      ->AttachSessionMainNativeFrameQueue(&queue);
}

void UnwireWindowCatalogMainNativeFrameQueue(ae::Obj& gui_root) {
  ForEachCatalogDesktopPresenter(gui_root, DetachQueue, nullptr);
}

void WireWindowCatalogMainNativeFrameQueueForHwnd(
    HWND hwnd, WindowCatalogMainNativeFrameQueue& queue) {
  LONG_PTR const user = GetWindowLongPtrW(hwnd, GWLP_USERDATA);
  if (user == 0) {
    return;
  }
  auto* win_presenter = static_cast<ui::windows::WinDesktopWindowPresenter*>(
      reinterpret_cast<void*>(user));
  auto* presenter = static_cast<example::surfaces::CatalogDesktopWindowPresenter*>(
      win_presenter);
  presenter->AttachSessionMainNativeFrameQueue(&queue);
}

bool WindowCatalogMainNativeFrameQueueAttached(ae::Obj& gui_root) {
  auto& catalog = static_cast<WindowCatalog&>(gui_root);
  catalog.persisted.main_window.Load();
  catalog.persisted.main_window->presenter.Load();
  if (!catalog.persisted.main_window->presenter) {
    return false;
  }
  ui::UiWindowPresenter* const ui_presenter =
      catalog.persisted.main_window->presenter.operator->();
  auto& registry = ae::Registry::GetRegistry();
  if (registry.GenerationDistance(
          ui::windows::WinDesktopWindowPresenter::kClassId,
          ui_presenter->GetClassId()) < 0) {
    return false;
  }
  auto* win_presenter =
      static_cast<ui::windows::WinDesktopWindowPresenter*>(ui_presenter);
  return static_cast<example::surfaces::CatalogDesktopWindowPresenter*>(
             win_presenter)
      ->SessionMainNativeFrameQueue() != nullptr;
}

}  // namespace apptraverse

