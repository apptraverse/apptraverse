#ifndef APPTRAVERSE_WINDOW_CATALOG_WEB_PERSISTENCE_H_
#define APPTRAVERSE_WINDOW_CATALOG_WEB_PERSISTENCE_H_

#include "aether-objects/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"

#include "window_catalog_model.h"

namespace apptraverse {

int PersistWindowCatalogEmbeddedUserOverlay(WindowCatalog& catalog,
                                            DirectoryDomainStorage& user_storage);
int PersistWindowCatalogEmbeddedUserOverlayFromDomain(
    ae::Domain& domain, DirectoryDomainStorage& user_storage);

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_CATALOG_WEB_PERSISTENCE_H_
