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
#include "apptraverse/platform/windows/win_presentation_host.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/platform/windows/win_ui_push_button_presenter.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/presenter.h"
#include "apptraverse/ui/ui_presented_window.h"
#include "apptraverse/ui/ui_presenters.h"
#include "apptraverse/noninteractive_crt.h"

namespace apptraverse::example::surfaces {
namespace {

::apptraverse::WindowCatalogMainNativeFrameQueue* CatalogQueueFromPresentationHost(
    void* presentation_host) {
  if (presentation_host == nullptr) {
    return nullptr;
  }
  auto* host =
      static_cast<ui::windows::WinPresentationHost*>(presentation_host);
  return host->catalog_main_native_frame_queue;
}

}  // namespace

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

  void OnLoad() override {
    ::apptraverse::WindowCatalogMainNativeFrameQueue* queue =
        SessionMainNativeFrameQueue();
    if (queue == nullptr) {
      queue = CatalogQueueFromPresentationHost(presentation_host);
    }
    assert(queue != nullptr);
    AttachSessionMainNativeFrameQueue(queue);
    ui::windows::WinDesktopWindowPresenter::OnLoad();
  }

  void ReportNativeFrameToModel(std::int32_t x, std::int32_t y,
                                std::int32_t client_width,
                                std::int32_t client_height,
                                bool updates_restore_geometry) override {
    if (this->model_proxy == nullptr) {
      return;
    }
    assert(catalog.is_valid() && this->window.is_valid());
    ::apptraverse::WindowCatalogMainNativeFrameQueue* queue =
        SessionMainNativeFrameQueue();
    if (queue == nullptr) {
      queue = CatalogQueueFromPresentationHost(presentation_host);
    }
    ae::ObjId const main_window_id = catalog->persisted.main_window.id();
    if (this->window->obj_id != main_window_id) {
      ui::UiDesktopWindowPresenter::ReportNativeFrameToModel(
          x, y, client_width, client_height, updates_restore_geometry);
      return;
    }
    assert(queue != nullptr);
    ::apptraverse::PostWindowCatalogMainNativeFrame(
        *queue, this->model_proxy, catalog->obj_id, this->window->obj_id, x, y,
        client_width, client_height, updates_restore_geometry);
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

void LoadCatalogRefsForPresenterWire(WindowCatalog& catalog) {
  catalog.persisted.main_window.Load();
  catalog.persisted.list.Load();
  catalog.persisted.presenters.list_presenter.Load();
  catalog.persisted.presenters.add_button_presenter.Load();
  catalog.persisted.presenters.open_button_presenter.Load();
  catalog.persisted.presenters.delete_button_presenter.Load();
  catalog.persisted.presenters.language_button_presenter.Load();
  catalog.persisted.window_prefab.Load();
  if (catalog.persisted.list) {
    catalog.persisted.list->rows;
    for (auto& row : catalog.persisted.list->rows) {
      row.Load();
    }
  }
  for (ae::ObjPtr<CatalogWindow> const& window :
       catalog.persisted.desktop_z_order) {
    window.Load();
  }
}

}  // namespace

void WireCatalogDesktopPresentersForLiveGraph(WindowCatalog& catalog,
                                              WindowCatalogMainNativeFrameQueue& queue) {
  WindowCatalog::ptr const catalog_ref =
      WindowCatalog::ptr::MakeFromThis(&catalog);
  std::vector<ae::Obj*> objects;
  CollectLiveReachableObjects(catalog, objects);
  auto& registry = ae::Registry::GetRegistry();
  for (ae::Obj* obj : objects) {
    if (registry.GenerationDistance(
            example::surfaces::CatalogDesktopWindowPresenter::kClassId,
            obj->GetClassId()) < 0) {
      continue;
    }
    auto* presenter =
        static_cast<example::surfaces::CatalogDesktopWindowPresenter*>(obj);
    presenter->catalog = catalog_ref;
    presenter->AttachSessionMainNativeFrameQueue(&queue);
    assert(presenter->SessionMainNativeFrameQueue() == &queue);
  }
}

void WireWindowCatalogMainNativeFrameQueue(
    ae::Obj& gui_root, WindowCatalogMainNativeFrameQueue& queue) {
  auto& catalog = static_cast<WindowCatalog&>(gui_root);
  LoadCatalogRefsForPresenterWire(catalog);
  catalog.AttachSessionMainNativeFrameQueue(&queue);
  WireCatalogDesktopPresentersForLiveGraph(catalog, queue);
  assert(catalog.SessionMainNativeFrameQueue() == &queue);
}

void UnwireWindowCatalogMainNativeFrameQueue(ae::Obj& gui_root) {
  auto& catalog = static_cast<WindowCatalog&>(gui_root);
  LoadCatalogRefsForPresenterWire(catalog);
  catalog.DetachSessionMainNativeFrameQueue();
  std::vector<ae::Obj*> objects;
  CollectLiveReachableObjects(catalog, objects);
  auto& registry = ae::Registry::GetRegistry();
  for (ae::Obj* obj : objects) {
    if (registry.GenerationDistance(
            example::surfaces::CatalogDesktopWindowPresenter::kClassId,
            obj->GetClassId()) < 0) {
      continue;
    }
    static_cast<example::surfaces::CatalogDesktopWindowPresenter*>(obj)
        ->DetachSessionMainNativeFrameQueue();
  }
}

bool WindowCatalogMainNativeFrameQueueAttached(ae::Obj& gui_root) {
  auto& catalog = static_cast<WindowCatalog&>(gui_root);
  return catalog.SessionMainNativeFrameQueue() != nullptr;
}

}  // namespace apptraverse
