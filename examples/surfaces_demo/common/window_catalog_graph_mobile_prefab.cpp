#include "window_catalog_graph_builder.h"

#include "window_catalog_ids.h"
#include "window_catalog_model.h"
#include "window_catalog_prefab_sizes.h"

#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_presented_window.h"

namespace apptraverse {
namespace {

// Model-space chrome for the child window Close control: full width minus
// margins, >=48dp row height at mdpi (Android scales X/Y from prefab client
// size; a short fixed height was crushed to ~28px after ScaleControlLayout).
constexpr std::int32_t kCloseButtonHeightMdpi = 48;
constexpr std::int32_t kCloseMarginMdpi = 12;
}  // namespace

// Builds the Close button graph and attaches it to the prefab window body
// (CatalogWindow::close_button / close_button_presenter). The button objects
// are persisted as part of the prefab graph save done by the builder.
void AppendMobileCloseButtonToWindowPrefab(
    ae::Domain& domain, WindowCatalog& catalog,
    CatalogWindow::ptr const& window_prefab,
    ui::UiDesktopWindowPresenter::ptr const& window_prefab_presenter,
    WindowCatalogPresenterFactory const& factory, std::int32_t prefab_client_height) {
  using example::surfaces::CatalogCloseButton;
  using window_catalog_demo::ObjId;
  using window_catalog_demo::ToObjId;

  std::int32_t const kCloseH = kCloseButtonHeightMdpi;
  std::int32_t const kCloseMargin = kCloseMarginMdpi;
  std::int32_t const kCloseW = kWindowCatalogPrefabClientWidth - 2 * kCloseMargin;
  std::int32_t const kCloseY = prefab_client_height - kCloseH - kCloseMargin;
  constexpr std::uint32_t kCloseControlId = 5006;

  auto close_button = CatalogCloseButton::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::WindowPrefabCloseButton)));
  close_button->BootstrapSetEnabled(true);
  // close_button->catalog stays unset in the prefab graph; AddWindow wires it
  // (and window) for every cloned holder window.
  close_button->window = window_prefab;
  close_button->label = catalog.persisted.texts.label_close;
  InitializeRuntimeNode(*close_button, catalog);

  ui::UiPushButtonPresenter::ptr close_presenter = factory.create_button(
      domain, ae::ObjId{ToObjId(ObjId::WindowPrefabCloseButtonPresenter)});
  close_presenter->button = close_button;
  close_presenter->window_presenter = window_prefab_presenter;
  close_presenter->layout_x = kCloseMargin;
  close_presenter->layout_y = kCloseY;
  close_presenter->layout_width = kCloseW;
  close_presenter->layout_height = kCloseH;
  close_presenter->control_id = kCloseControlId;

  CatalogWindow::ptr prefab_ref = window_prefab;
  ae::Ptr<CatalogWindow> const window_body = prefab_ref.Load();
  window_body->close_button = close_button;
  window_body->close_button_presenter = close_presenter;
}

}  // namespace apptraverse
