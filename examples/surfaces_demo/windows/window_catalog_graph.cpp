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
#include "apptraverse/platform/windows/win_ui_push_button_presenter.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/presenter.h"
#include "apptraverse/ui/ui_presenters.h"
#include "apptraverse/noninteractive_crt.h"

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

  void OnLoad() override {
    assert(SessionMainNativeFrameQueue() != nullptr);
    assert(session_catalog_main_window_id_.is_valid());
    ui::windows::WinDesktopWindowPresenter::OnLoad();
  }

  void ReportNativeFrameToModel(std::int32_t x, std::int32_t y,
                                std::int32_t client_width,
                                std::int32_t client_height,
                                bool updates_restore_geometry) override {
    if (this->model_proxy == nullptr) {
      return;
    }
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
    if (!catalog.is_valid() || !this->window.is_valid()) {
      return;
    }
    ui::UiDesktopWindowPresenter::ReportNativeFrameToModel(
        x, y, client_width, client_height, updates_restore_geometry);
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

void WireWindowCatalogMainNativeFrameQueue(
    ae::Obj& gui_root, WindowCatalogMainNativeFrameQueue& queue) {
  ae::Domain* const domain = gui_root.domain;
  assert(domain != nullptr);
  auto& catalog = static_cast<WindowCatalog&>(gui_root);
  ae::ObjId const catalog_obj_id = gui_root.obj_id;
  ae::ObjId const main_window_id = catalog.persisted.main_window.id();
  std::vector<ae::Obj*> objects;
  CollectLiveReachableObjects(gui_root, objects);
  auto& registry = ae::Registry::GetRegistry();
  std::size_t attached = 0;
  for (ae::Obj* obj : objects) {
    if (registry.GenerationDistance(
            example::surfaces::CatalogDesktopWindowPresenter::kClassId,
            obj->GetClassId()) < 0) {
      continue;
    }
    auto* presenter =
        static_cast<example::surfaces::CatalogDesktopWindowPresenter*>(obj);
    presenter->AttachSessionMainNativeFrameQueue(&queue, catalog_obj_id,
                                                main_window_id);
    assert(presenter->SessionMainNativeFrameQueue() == &queue);
    ++attached;
  }
  assert(attached > 0);
}

void UnwireWindowCatalogMainNativeFrameQueue(ae::Obj& gui_root) {
  ae::Domain* const domain = gui_root.domain;
  if (domain == nullptr) {
    return;
  }
  std::vector<ae::Obj*> objects;
  CollectLiveReachableObjects(gui_root, objects);
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

bool WindowCatalogMainNativeFrameQueuePrepareBinding(
    ae::Obj& gui_root, ae::ObjId main_presenter_id,
    WindowCatalogMainNativeFrameQueue& queue, bool require_presentation_pending) {
  ae::Domain* const domain = gui_root.domain;
  if (domain == nullptr || !main_presenter_id.is_valid()) {
    return false;
  }
  auto held = domain->Find(main_presenter_id);
  if (!held) {
    return false;
  }
  ae::Obj* const domain_presenter = &*held;
  auto& catalog = static_cast<WindowCatalog&>(gui_root);
  ae::ObjId const catalog_obj_id = gui_root.obj_id;
  ae::ObjId const main_window_id = catalog.persisted.main_window.id();
  std::vector<ae::Obj*> objects;
  CollectLiveReachableObjects(gui_root, objects);
  auto& registry = ae::Registry::GetRegistry();
  ae::Obj* reachable_presenter = nullptr;
  for (ae::Obj* obj : objects) {
    if (registry.GenerationDistance(
            example::surfaces::CatalogDesktopWindowPresenter::kClassId,
            obj->GetClassId()) < 0) {
      continue;
    }
    auto* presenter =
        static_cast<example::surfaces::CatalogDesktopWindowPresenter*>(obj);
    if (presenter->window.id() != main_window_id) {
      continue;
    }
    reachable_presenter = obj;
    break;
  }
  if (reachable_presenter == nullptr || reachable_presenter != domain_presenter) {
    return false;
  }
  auto& catalog_presenter =
      static_cast<example::surfaces::CatalogDesktopWindowPresenter&>(*held);
  if (catalog_presenter.SessionMainNativeFrameQueue() != &queue) {
    return false;
  }
  if (require_presentation_pending && catalog_presenter.presentation_loaded) {
    return false;
  }
  return true;
}

bool WindowCatalogMainNativeFrameQueueAttached(ae::Obj& gui_root) {
  auto& catalog = static_cast<WindowCatalog&>(gui_root);
  ae::ObjId const catalog_obj_id = gui_root.obj_id;
  ae::ObjId const main_window_id = catalog.persisted.main_window.id();
  std::vector<ae::Obj*> objects;
  CollectLiveReachableObjects(gui_root, objects);
  auto& registry = ae::Registry::GetRegistry();
  for (ae::Obj* obj : objects) {
    if (registry.GenerationDistance(
            example::surfaces::CatalogDesktopWindowPresenter::kClassId,
            obj->GetClassId()) < 0) {
      continue;
    }
    auto* presenter =
        static_cast<example::surfaces::CatalogDesktopWindowPresenter*>(obj);
    if (presenter->window.id() != main_window_id) {
      continue;
    }
    return presenter->SessionMainNativeFrameQueue() != nullptr;
  }
  return false;
}

}  // namespace apptraverse
