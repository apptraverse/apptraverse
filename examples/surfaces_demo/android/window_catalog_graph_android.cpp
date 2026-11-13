#include "window_catalog_graph_android.h"
#include "window_catalog_presenters_android.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/platform/android/android_ui_registration.h"

namespace apptraverse::example::surfaces {
void AndroidCatalogListContainerPresenter::SubmitListRowSelectionChangedFromUi(
    std::size_t row_index) {
  auto const row_id = RowIdAtUiIndex(row_index);
  if (!row_id) return;
  model_proxy->Invoke(catalog->obj_id, &WindowCatalog::SubmitSelectionByIdFromUi,
                      *row_id);
}
void AndroidCatalogListContainerPresenter::SubmitListRowActivatedFromUi(
    std::size_t row_index) {
  auto const row_id = RowIdAtUiIndex(row_index);
  if (!row_id) return;
  model_proxy->Invoke(catalog->obj_id, &WindowCatalog::OpenHolderByIdFromUi,
                      *row_id);
}
APPTRAVERSE_REGISTER(CatalogDesktopWindowPresenter);
APPTRAVERSE_REGISTER(AndroidCatalogListContainerPresenter);
}  // namespace apptraverse::example::surfaces

namespace apptraverse {
void EnsureWindowCatalogAndroidGraphRegistration() {
  EnsureCoreObjectRegistration();
  EnsureWindowCatalogModelRegistration();
  ui::android::EnsureAndroidUiPresenterRegistration();
  (void)&example::surfaces::g_apptraverse_registrar_AndroidCatalogListContainerPresenter;
  (void)&example::surfaces::g_apptraverse_registrar_CatalogDesktopWindowPresenter;
}
}  // namespace apptraverse
