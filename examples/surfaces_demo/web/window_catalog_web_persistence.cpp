#include "window_catalog_web_persistence.h"

#include "window_catalog_ids.h"
#include "window_catalog_model.h"

#include "apptraverse/noninteractive_crt.h"

namespace apptraverse {

int PersistWindowCatalogEmbeddedUserOverlay(WindowCatalog& catalog,
                                            DirectoryDomainStorage& user_storage) {
  user_storage.BeginSaveSession();
  SaveWindowCatalogPersistentSnapshot(catalog);
  if (user_storage.SaveSessionHadFailure()) {
    WriteFatalStderr("fatal: WindowCatalog Web USER overlay persist failed\n");
    return 1;
  }
  return 0;
}

int PersistWindowCatalogEmbeddedUserOverlayFromDomain(
    ae::Domain& domain, DirectoryDomainStorage& user_storage) {
  ae::ObjId const catalog_id{
      window_catalog_demo::ToObjId(window_catalog_demo::ObjId::Catalog)};
  ae::Ptr<ae::Obj> const held = domain.Find(catalog_id);
  if (!held) {
    WriteFatalStderr("fatal: WindowCatalog missing for Web persist\n");
    return 1;
  }
  return PersistWindowCatalogEmbeddedUserOverlay(
      static_cast<WindowCatalog&>(*held), user_storage);
}

}  // namespace apptraverse
