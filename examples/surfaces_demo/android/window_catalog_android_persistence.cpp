#include "window_catalog_android_persistence.h"

#include "window_catalog_ids.h"
#include "window_catalog_model.h"
#include "window_catalog_prepare.h"

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/single_file_user_storage.h"

namespace apptraverse::example::surfaces {

int PersistWindowCatalogEmbeddedUserOverlayFromDomain(
    ae::Domain& domain, WindowCatalogAndroidEmbeddedProfile& profile) {
  ae::ObjId const catalog_id{
      window_catalog_demo::ToObjId(window_catalog_demo::ObjId::Catalog)};
  ae::Ptr<ae::Obj> const held = domain.Find(catalog_id);
  if (!held) {
    WriteFatalStderr("fatal: WindowCatalog missing for Android persist\n");
    return 1;
  }
  auto& catalog = static_cast<apptraverse::WindowCatalog&>(*held);
  SaveWindowCatalogPersistentSnapshot(catalog);
  if (!SaveUserStorageFile(profile.user, profile.state_dir)) {
    WriteFatalStderr("fatal: WindowCatalog Android checkpoint save failed\n");
    return 1;
  }
  return 0;
}

}  // namespace apptraverse::example::surfaces
