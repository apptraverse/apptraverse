#include "window_catalog_graph_builder.h"

#include "window_catalog_close_button.h"
#include "window_catalog_ids.h"
#include "window_catalog_localization.h"
#include "window_catalog_model.h"

#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_presented_window.h"

namespace apptraverse {
namespace {

constexpr std::int32_t kButtonH = 28;
constexpr std::int32_t kButtonW = 90;

}  // namespace

void AppendMobileCloseButtonToWindowPrefab(
    ae::Domain& domain, WindowCatalog& catalog,
    CatalogWindow::ptr const& window_prefab,
    ui::UiDesktopWindowPresenter::ptr const& window_prefab_presenter,
    WindowCatalogPresenterFactory const& factory, std::int32_t prefab_client_height) {
  using example::surfaces::CatalogCloseButton;
  using window_catalog_demo::ObjId;
  using window_catalog_demo::ToObjId;

  std::int32_t const kCloseY = prefab_client_height - kButtonH - 12;
  constexpr std::uint32_t kCloseControlId = 5006;
  auto close_button = CatalogCloseButton::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::WindowPrefabCloseButton)));
  close_button->catalog = WindowCatalog::ptr::MakeFromThis(&catalog);
  close_button->window = window_prefab;
  close_button->label =
      CreateCatalogLocalizedString(domain, catalog, "Close", "Закрыть");
  InitializeRuntimeNode(*close_button, catalog);
  ui::UiPushButtonPresenter::ptr close_presenter = factory.create_button(domain);
  close_presenter->button = close_button;
  close_presenter->window_presenter = window_prefab_presenter;
  close_presenter->layout_x = 12;
  close_presenter->layout_y = kCloseY;
  close_presenter->layout_width = kButtonW;
  close_presenter->layout_height = kButtonH;
  close_presenter->control_id = kCloseControlId;
  ae::DomainGraph close_distill{&domain};
  ae::Ptr<ui::UiPushButtonPresenter> close_presenter_body = close_presenter.Load();
  ae::Ptr<CatalogCloseButton> close_button_body = close_button.Load();
  if (!close_distill
           .SaveRoot(close_presenter_body,
                     ToObjId(ObjId::WindowPrefabCloseButtonPresenter))
           .IsOk() ||
      !close_distill
           .SaveRoot(close_button_body, ToObjId(ObjId::WindowPrefabCloseButton))
           .IsOk()) {
    std::abort();
  }
  domain.RemoveObject(close_presenter_body.get());
  domain.RemoveObject(close_button_body.get());
}

}  // namespace apptraverse
