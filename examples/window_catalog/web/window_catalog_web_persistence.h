#ifndef APPTRAVERSE_WINDOW_CATALOG_WEB_PERSISTENCE_H_
#define APPTRAVERSE_WINDOW_CATALOG_WEB_PERSISTENCE_H_

#include "aether-objects/obj/domain.h"

#include <filesystem>
#include "aether-objects/domain_storage/ram_domain_storage.h"

#include "window_catalog_model.h"

namespace apptraverse {

int PersistWindowCatalogEmbeddedUserOverlay(WindowCatalog& catalog,
    ae::RamDomainStorage const& user_storage, std::filesystem::path const& profile);

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_CATALOG_WEB_PERSISTENCE_H_
