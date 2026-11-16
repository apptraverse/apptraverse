#include "window_catalog_graph.h"

#include "window_catalog_actions.h"
#include "window_catalog_main_native_frame_queue.h"
#include "window_catalog_main_native_frame_wiring.h"
#include "window_catalog_ids.h"
#include "window_catalog_list_presenter_mixin.h"
#include "window_catalog_model.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/platform/linux/linux_ui_registration.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/presenter.h"
#include "apptraverse/ui/ui_presenters.h"
#include "apptraverse/noninteractive_crt.h"

#include "window_catalog_presenters_linux.h"

namespace apptraverse::example::surfaces {

void CatalogListContainerPresenter::SubmitListRowSelectionChangedFromUi(
    std::size_t row_index) {
  auto const row_id = RowIdAtUiIndex(row_index);
  if (!row_id) { return; }
  model_proxy->Invoke(catalog->obj_id, &WindowCatalog::SubmitSelectionByIdFromUi,
                      *row_id);
}

void CatalogListContainerPresenter::SubmitListRowActivatedFromUi(
    std::size_t row_index) {
  auto const row_id = RowIdAtUiIndex(row_index);
  if (!row_id) { return; }
  model_proxy->Invoke(catalog->obj_id, &WindowCatalog::OpenHolderByIdFromUi, *row_id);
}

APPTRAVERSE_REGISTER(CatalogDesktopWindowPresenter);
APPTRAVERSE_REGISTER(CatalogListContainerPresenter);

}  // namespace apptraverse::example::surfaces

namespace apptraverse {

void EnsureWindowCatalogGraphRegistration() {
  // The single Linux application entry point: all transitive schema anchors.
  EnsureCoreObjectRegistration();
  EnsureWindowCatalogDesktopModelRegistration();
  ui::linux::EnsureLinuxUiPresenterRegistration();
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


}  // namespace apptraverse
