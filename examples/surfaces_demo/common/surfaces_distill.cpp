#include "surfaces_distill.h"

#include "surfaces_ids.h"
#include "surfaces_localization.h"
#include "surfaces_presentation.h"



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
  surfaces->surfaces.push_back(surface1);
  application->surfaces = surfaces;

  InstallSurfacesDemoLocalization(*application, domain);
  AttachPlatformSurfacePresenters(*surface1, *application, domain);

  return application;

}



}  // namespace apptraverse

