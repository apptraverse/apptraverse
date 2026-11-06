#include "window_catalog_android_persistence.h"

#include "window_catalog_ids.h"
#include "window_catalog_model.h"
#include "window_catalog_prepare.h"

#include "apptraverse/distill.h"
#include "apptraverse/noninteractive_crt.h"

namespace apptraverse::example::surfaces {

void NormalizeWindowCatalogAfterEmbeddedUserLoad(
    apptraverse::WindowCatalog& catalog) {
#if defined(APPTRAVERSE_ENABLE_DISTILLATION)
  FinalizeDistilledGraph(catalog);
#else
  (void)catalog;
#endif
}

int PersistWindowCatalogEmbeddedUserOverlayFromDomain(
    ae::Domain& domain, DirectoryDomainStorage& user_storage) {
  ae::ObjId const catalog_id{
      window_catalog_demo::ToObjId(window_catalog_demo::ObjId::Catalog)};
  ae::Ptr<ae::Obj> const held = domain.Find(catalog_id);
  if (!held) {
    WriteFatalStderr("fatal: WindowCatalog missing for Android persist\n");
    return 1;
  }
  auto& catalog = static_cast<apptraverse::WindowCatalog&>(*held);
  user_storage.BeginSaveSession();
  SaveWindowCatalogPersistentSnapshot(catalog);
  if (user_storage.SaveSessionHadFailure()) {
    WriteFatalStderr("fatal: WindowCatalog Android checkpoint save failed\n");
    return 1;
  }
  return 0;
}

}  // namespace apptraverse::example::surfaces
