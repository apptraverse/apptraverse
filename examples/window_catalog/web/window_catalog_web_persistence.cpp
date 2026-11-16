#include "window_catalog_web_persistence.h"
#include "apptraverse/single_file_user_storage.h"

namespace apptraverse {
int PersistWindowCatalogEmbeddedUserOverlay(WindowCatalog& catalog,
    ae::RamDomainStorage const& user_storage, std::filesystem::path const& profile) {
  SaveWindowCatalogPersistentSnapshot(catalog);
  return SaveUserStorageFile(user_storage, profile) ? 0 : 1;
}
}  // namespace apptraverse
