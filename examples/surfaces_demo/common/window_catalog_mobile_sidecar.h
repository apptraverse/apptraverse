#ifndef APPTRAVERSE_WINDOW_CATALOG_MOBILE_SIDECAR_H_
#define APPTRAVERSE_WINDOW_CATALOG_MOBILE_SIDECAR_H_

#include "window_catalog_model.h"

#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"

namespace apptraverse {

class WindowCatalogMobileSidecar;

class SetWindowCatalogMobileActiveEvent
    : public EventFor<WindowCatalogMobileSidecar,
                      SetWindowCatalogMobileActiveEvent> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::SetWindowCatalogMobileActiveEvent",
      SetWindowCatalogMobileActiveEvent, Event, 0)

 protected:
  SetWindowCatalogMobileActiveEvent() = default;

 public:
  explicit SetWindowCatalogMobileActiveEvent(ae::ObjProp prop)
      : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(window))

  CatalogWindow::ptr window;
};

class WindowCatalogMobileSidecar : public NodeFor<WindowCatalogMobileSidecar> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::WindowCatalogMobileSidecar",
      WindowCatalogMobileSidecar, Node, 0)

 protected:
  WindowCatalogMobileSidecar() = default;

 public:
  explicit WindowCatalogMobileSidecar(ae::ObjProp prop)
      : NodeFor{prop}, active_window_id{0} {}

  AE_OBJECT_REFLECT(AE_MMBR(catalog), AE_MMBR(active_window_id))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, catalog, active_window_id);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, catalog, active_window_id);
  }

  WindowCatalog::ptr catalog;
  std::uint32_t active_window_id;

  void Apply(SetWindowCatalogMobileActiveEvent const& event);
  void SetActiveWindow(CatalogWindow::ptr window);
  CatalogWindow::ptr ResolveActiveWindow();
};

WindowCatalogMobileSidecar* FindMobileSidecar(WindowCatalog& catalog);

void EnsureWindowCatalogMobileSidecarRegistration();

WindowCatalogMobileSidecar::ptr CreateWindowCatalogMobileSidecar(
    ae::Domain& domain, WindowCatalog& catalog);

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_CATALOG_MOBILE_SIDECAR_H_
