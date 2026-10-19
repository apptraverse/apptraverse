#include "surfaces_distill.h"

#include "apptraverse/surfaces/surfaces_ids.h"
#include "apptraverse/surfaces/surfaces_localization.h"
#include "surfaces_demo_platform_attach.h"



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
  surfaces->host_application = &*application;
  WireSurfacesDemoPlatformBindings(*surfaces);

  InstallSurfacesDemoLocalization(*application, domain);
  AttachSurfacesDemoPlatformPresenters(*surface1, *application, domain);

  return application;

}



}  // namespace apptraverse

