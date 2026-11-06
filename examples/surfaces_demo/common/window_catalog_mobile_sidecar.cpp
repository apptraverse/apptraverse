#include "window_catalog_mobile_sidecar.h"

#include "window_catalog_ids.h"

#include "apptraverse/object_macros.h"
#include "apptraverse/runtime_node.h"

namespace apptraverse {

APPTRAVERSE_REGISTER(WindowCatalogMobileSidecar);
APPTRAVERSE_REGISTER(SetWindowCatalogMobileActiveEvent);

WindowCatalogMobileSidecar* FindMobileSidecar(WindowCatalog& catalog) {
  ae::ObjId const sidecar_id{
      window_catalog_demo::ToObjId(window_catalog_demo::ObjId::MobileSidecar)};
  ae::Ptr<ae::Obj> held = catalog.domain->Find(sidecar_id);
  if (!held) {
    return nullptr;
  }
  return static_cast<WindowCatalogMobileSidecar*>(held.get());
}

void WindowCatalogMobileSidecar::Apply(
    SetWindowCatalogMobileActiveEvent const& event) {
  if (!event.window.is_valid()) {
    active_window_id = catalog->main_window.id().id();
  } else {
    active_window_id = event.window.id().id();
  }
  NoteMaterializedChange();
}

void WindowCatalogMobileSidecar::SetActiveWindow(CatalogWindow::ptr window) {
  auto event = SetWindowCatalogMobileActiveEvent::ptr::Create(
      ae::CreateWith{*domain});
  event->window = std::move(window);
  Commit(std::move(event));
}

CatalogWindow::ptr WindowCatalogMobileSidecar::ResolveActiveWindow() {
  catalog.Load();
  ae::ObjId const main_id = catalog->main_window.id();
  ae::ObjId const active_id{active_window_id};
  if (!active_id.is_valid() || active_id == main_id) {
    return catalog->main_window;
  }
  WindowHolder* holder = catalog->FindHolderForWindow(active_id);
  if (holder == nullptr || !holder->window.is_valid()) {
    return catalog->main_window;
  }
  return holder->window;
}

void EnsureWindowCatalogMobileSidecarRegistration() {
  (void)&g_apptraverse_registrar_WindowCatalogMobileSidecar;
  (void)&g_apptraverse_registrar_SetWindowCatalogMobileActiveEvent;
}

WindowCatalogMobileSidecar::ptr CreateWindowCatalogMobileSidecar(
    ae::Domain& domain, WindowCatalog& catalog) {
  auto sidecar = WindowCatalogMobileSidecar::ptr::Create(
      ae::CreateWith{domain}.with_id(window_catalog_demo::ToObjId(
          window_catalog_demo::ObjId::MobileSidecar)));
  sidecar->catalog = WindowCatalog::ptr::MakeFromThis(&catalog);
  sidecar->active_window_id = catalog.main_window.id().id();
  InitializeRuntimeNode(*sidecar, catalog);
  return sidecar;
}

}  // namespace apptraverse
