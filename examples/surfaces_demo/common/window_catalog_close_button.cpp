#include "window_catalog_close_button.h"
#include "window_catalog_chrome_layout.h"

#include "apptraverse/object_macros.h"
#include "apptraverse/model_object_proxy.h"

namespace apptraverse {

namespace {
CatalogWindow* LiveWindowForNativeInput(WindowCatalog& catalog, ae::ObjId id) {
  if (id == catalog.persisted.main_window.id()) {
    return catalog.persisted.main_window.is_loaded()
        ? catalog.persisted.main_window.operator->() : nullptr;
  }
  auto* holder = catalog.FindHolderForWindow(id);
  return holder != nullptr && catalog.IsHolderWindowOpen(*holder) &&
      holder->window.is_loaded() ? holder->window.operator->() : nullptr;
}
}  // namespace

void WindowCatalog::NotifyChildCloseButtonFromUi(std::uint32_t window_obj_id) {
  ae::ObjId const id{window_obj_id};
  if (id == persisted.main_window.id()) { return; }
  auto* window = LiveWindowForNativeInput(*this, id);
  if (window == nullptr) { return; }
  ae::Ptr<example::surfaces::CatalogCloseButton> button = window->close_button.Load();
  button->NotifyClicked();
}

void WindowCatalog::SubmitHolderChildWindowFrameFromUi(
    std::uint32_t window_obj_id, std::int32_t x, std::int32_t y,
    std::int32_t width, std::int32_t height, bool updates_restore_geometry) {
  ae::ObjId const id{window_obj_id};
  if (id == persisted.main_window.id() || width <= 0 || height <= 0) {
    return;
  }
  auto* window = LiveWindowForNativeInput(*this, id);
  if (window == nullptr) { return; }
  window->SubmitFrameFromNative(x, y, width, height, updates_restore_geometry);
  SubmitHolderChildWindowClientSizeFromUi(window_obj_id, width, height, 160);
}

void WindowCatalog::SubmitWindowScreenFromUi(
    std::uint32_t window_obj_id, std::int32_t x, std::int32_t y,
    std::int32_t width, std::int32_t height) {
  auto* window = LiveWindowForNativeInput(*this, ae::ObjId{window_obj_id});
  if (window == nullptr) { return; }
  window->SubmitScreenFromNative(x, y, width, height);
}

void WindowCatalog::SubmitHolderChildWindowClientSizeFromUi(
    std::uint32_t window_obj_id, std::int32_t width, std::int32_t height,
    std::int32_t density_dpi) {
  if (width <= 0 || height <= 0) {
    return;
  }
  ae::ObjId const wid{window_obj_id};
  if (!wid.is_valid() || wid == persisted.main_window.id()) {
    return;
  }
  // A queued native measurement may arrive after Close unloaded the graph.
  // Validate current catalog membership before borrowing its window; Ptr::as
  // requires a loaded object and cannot be called on an empty Domain::Find.
  WindowHolder* holder = FindHolderForWindow(wid);
  if (holder == nullptr || !IsHolderWindowOpen(*holder) ||
      !holder->window.is_loaded()) {
    return;
  }
  auto const& window = holder->window;
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

void CatalogCloseButton::QueueClickFromUi(ModelObjectProxy& proxy) const {
  // The button can disappear before an already queued click reaches the model.
  // The catalog owns this action's lifetime and checks current window membership.
  proxy.Invoke(catalog.id(), &WindowCatalog::NotifyChildCloseButtonFromUi,
               window.id().id());
}

bool CatalogCloseButton::VisibleInCurrentPresentation() const {
  return catalog->persisted.mobile_pager;
}

void CatalogCloseButton::HandlePushButtonClicked() {
  WindowCatalog& cat = LoadedCatalog(catalog);
  cat.UserCloseWindow(window.id());
}

APPTRAVERSE_REGISTER(CatalogCloseButton);

void EnsureCatalogCloseButtonRegistration() {
  (void)&g_apptraverse_registrar_CatalogCloseButton;
}

}  // namespace apptraverse::example::surfaces
