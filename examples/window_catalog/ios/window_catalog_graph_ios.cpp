#include "window_catalog_graph.h"

#include "window_catalog_list_presenter_mixin.h"
#include "window_catalog_model.h"
#include "window_catalog_menu.h"
#include "window_catalog_presenters_ios.h"
#include "window_catalog_settings_ios.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/object_macros.h"

namespace apptraverse::example::surfaces {

void IosCatalogListPresenter::SubmitListRowSelectionChangedFromUi(
    std::size_t row_index) {
  auto const row_id = RowIdAtUiIndex(row_index);
  if (!row_id) return;
  model_proxy->Invoke(catalog->obj_id, &WindowCatalog::SubmitSelectionByIdFromUi,
                      *row_id);
}

void IosCatalogListPresenter::SubmitListRowActivatedFromUi(
    std::size_t row_index) {
  auto const row_id = RowIdAtUiIndex(row_index);
  if (!row_id) return;
  model_proxy->Invoke(catalog->obj_id, &WindowCatalog::OpenHolderByIdFromUi,
                      *row_id);
}

APPTRAVERSE_REGISTER(IosCatalogWindowPresenter);
APPTRAVERSE_REGISTER(IosCatalogListPresenter);

}  // namespace apptraverse::example::surfaces

namespace apptraverse {

void IosCatalogUiState::SetSettingsPresentedFromUi(bool presented) {
  if (settings_presented == presented) return;
  auto event = SetIosCatalogSettingsPresentedEvent::ptr::Create(ae::CreateWith{*domain});
  event->presented = presented;
  CommitLocal(std::move(event));
}

void IosCatalogUiState::Apply(SetIosCatalogSettingsPresentedEvent const& event) {
  settings_presented = event.presented;
  NoteMaterializedChange();
}

APPTRAVERSE_REGISTER(IosCatalogUiState);
APPTRAVERSE_REGISTER(SetIosCatalogSettingsPresentedEvent);

void EnsureWindowCatalogGraphRegistration() {
  EnsureCoreObjectRegistration();
  EnsureWindowCatalogNativeMenuModelRegistration();
  EnsureCatalogMenuRegistration();
  ui::ios::EnsureIosUiPresenterRegistration();
  (void)&g_apptraverse_registrar_IosCatalogUiState;
  (void)&g_apptraverse_registrar_SetIosCatalogSettingsPresentedEvent;
  (void)&example::surfaces::g_apptraverse_registrar_IosCatalogWindowPresenter;
  (void)&example::surfaces::g_apptraverse_registrar_IosCatalogListPresenter;
}

}  // namespace apptraverse
