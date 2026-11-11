#include "window_catalog_close_button.h"
#include "window_catalog_chrome_layout.h"

#include "apptraverse/object_macros.h"

namespace apptraverse {

void WindowCatalog::SubmitHolderChildWindowClientSizeFromUi(
    std::uint32_t window_obj_id, std::int32_t width, std::int32_t height,
    std::int32_t density_dpi) {
  if (!persisted.mobile_pager || width <= 0 || height <= 0) {
    return;
  }
  ae::ObjId const wid{window_obj_id};
  if (!wid.is_valid() || wid == persisted.main_window.id()) {
    return;
  }
  auto window = domain->Find(wid).as<CatalogWindow>();
  if (window == nullptr) {
    return;
  }
  WindowHolder* holder = FindHolderForWindow(window->obj_id);
  if (holder == nullptr || !holder->window.is_loaded()) {
    return;
  }
  auto const layout = ComputeWindowCatalogChildCloseLayout(width, height, density_dpi);
  auto const& presenter = *window->close_button_presenter;
  if (window->client_width() == width && window->client_height() == height &&
      presenter.layout_x == layout.x && presenter.layout_y == layout.y &&
      presenter.layout_width == layout.width &&
      presenter.layout_height == layout.height) {
    return;
  }
  auto event = SetCatalogChildViewportEvent::ptr::Create(ae::CreateWith{*domain});
  event->width = width;
  event->height = height;
  event->density_dpi = density_dpi;
  window->Commit(std::move(event));
}

void CatalogWindow::Apply(SetCatalogChildViewportEvent const& event) {
  SetClientSizeInternal(event.width, event.height);
  auto const layout = ComputeWindowCatalogChildCloseLayout(
      event.width, event.height, event.density_dpi);
  auto& presenter = *close_button_presenter;
  presenter.layout_x = layout.x;
  presenter.layout_y = layout.y;
  presenter.layout_width = layout.width;
  presenter.layout_height = layout.height;
  NoteMaterializedChange();
}

}  // namespace apptraverse

namespace apptraverse::example::surfaces {
namespace {

WindowCatalog& LoadedCatalog(WindowCatalog::ptr& catalog) {
  if (!catalog.is_loaded()) {
    catalog.Load();
  }
  return *catalog;
}

}  // namespace

void CatalogCloseButton::HandlePushButtonClicked() {
  WindowCatalog& cat = LoadedCatalog(catalog);
  cat.UserCloseWindow(window.id());
}

APPTRAVERSE_REGISTER(CatalogCloseButton);

void EnsureCatalogCloseButtonRegistration() {
  (void)&g_apptraverse_registrar_CatalogCloseButton;
}

}  // namespace apptraverse::example::surfaces
