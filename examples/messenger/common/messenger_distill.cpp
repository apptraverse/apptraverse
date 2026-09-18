#include "messenger_distill.h"

#include "messenger_ids.h"

namespace apptraverse {

Application::ptr BuildMessengerGraph(ae::Domain& domain) {
  using messenger::ObjId;
  using messenger::ToObjId;

  auto application = Application::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::Application)));
  auto surfaces = Surfaces::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::Surfaces)));
  auto surface1 = Surface::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::Surface1)));
  auto surface1_presenter = SurfacePresenter::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::Surface1Presenter)));

  surface1->number = 1;
  AssignInitialDesktopBounds(*surface1);
  surface1->surfaces = surfaces;
  surface1->presenter = surface1_presenter;
  surface1_presenter->surface = surface1;
  surfaces->surfaces.push_back(surface1);

  application->surfaces = surfaces;
  return application;
}

}  // namespace apptraverse
