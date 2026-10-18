#include "surfaces_distill.h"

#include "apptraverse/distill.h"

#include "surface_button_presenter.h"
#include "surface_window_presenter.h"
#include "surfaces_ids.h"
#include "surfaces_presentation.h"

namespace apptraverse {

void RewireSurfaceChromePresenters(Surface& surface) {
  auto surface_ref = Surface::ptr::MakeFromThis(&surface);
  if (surface.chrome.window) {
    surface.chrome.window->surface = surface_ref;
  }
  if (SurfaceButtonPresenter::ptr add{surface.chrome.add_button}) {
    add->surface = surface_ref;
    if (surface.chrome.window) {
      add->window_presenter =
          SurfaceWindowPresenter::ptr{surface.chrome.window};
    }
  }
  if (SurfaceButtonPresenter::ptr close{surface.chrome.close_button}) {
    close->surface = surface_ref;
    if (surface.chrome.window) {
      close->window_presenter =
          SurfaceWindowPresenter::ptr{surface.chrome.window};
    }
  }
}

void FinalizeSurfacesDistilledGraph(Application& application) {
  FinalizeDistilledGraph(application);
  for (auto const& surface : application.surfaces->surfaces) {
    RewireSurfaceChromePresenters(*surface);
  }
}

}  // namespace apptraverse

namespace apptraverse {

Application::ptr BuildSurfacesGraph(ae::Domain& domain) {
  using surfaces_demo::ObjId;
  using surfaces_demo::ToObjId;

  auto application = Application::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::Application)));
  auto surfaces = Surfaces::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::Surfaces)));
  auto surface1 = Surface::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::Surface1)));

  surface1->number = 1;
  AssignInitialDesktopBounds(*surface1);
  surface1->surfaces = surfaces;
  AttachPlatformSurfacePresenters(*surface1, domain);
  surfaces->surfaces.push_back(surface1);

  application->surfaces = surfaces;
  return application;
}

}  // namespace apptraverse
