#include "window_catalog_graph_web.h"
#include "window_catalog_presenters_web.h"
#include "window_catalog_ids.h"
#include "apptraverse/model_object_proxy.h"
#include <emscripten/em_js.h>

namespace apptraverse::example::surfaces {
namespace {
EM_JS(void, ApptraverseCatalogTagWindowElement,
      (emscripten::EM_VAL client_element, unsigned window_id, int is_main), {
  var win = Emval.toValue(client_element).parentNode;
  // Wasm i32 crosses the JS boundary as signed; ObjId remains uint32.
  win.dataset.windowId = String(window_id >>> 0);
  win.classList.add('catalog-page');
  if (is_main) win.classList.add('catalog-page-main');
});
}

void CatalogDesktopWindowPresenter::OnLoad() {
  WebDesktopWindowPresenter::OnLoad();
  auto const id = desktop_window().id();
  ApptraverseCatalogTagWindowElement(ClientMountHandle(), id.id(),
      id.id() == window_catalog_demo::ToObjId(window_catalog_demo::ObjId::MainWindow));
}
void WebCatalogListContainerPresenter::SubmitListRowSelectionChangedFromUi(std::size_t index) {
  auto const id = RowIdAtUiIndex(index);
  if (!id) return;
  model_proxy->Invoke(catalog->obj_id, &WindowCatalog::SubmitSelectionByIdFromUi, *id);
}
void WebCatalogListContainerPresenter::SubmitListRowActivatedFromUi(std::size_t index) {
  auto const id = RowIdAtUiIndex(index);
  if (!id) return;
  model_proxy->Invoke(catalog->obj_id, &WindowCatalog::OpenHolderByIdFromUi, *id);
}
APPTRAVERSE_REGISTER(CatalogDesktopWindowPresenter);
APPTRAVERSE_REGISTER(WebCatalogListContainerPresenter);
}  // namespace apptraverse::example::surfaces

namespace apptraverse {
void EnsureWebWindowCatalogGraphRegistration() {
  EnsureCoreObjectRegistration();
  EnsureWindowCatalogModelRegistration();
  ui::web::EnsureWebUiPresenterRegistration();
  (void)&example::surfaces::g_apptraverse_registrar_CatalogDesktopWindowPresenter;
  (void)&example::surfaces::g_apptraverse_registrar_WebCatalogListContainerPresenter;
}
}  // namespace apptraverse
