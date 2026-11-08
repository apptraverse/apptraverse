#ifndef APPTRAVERSE_EXAMPLE_SURFACES_WINDOW_CATALOG_ANDROID_PERSISTENCE_H_
#define APPTRAVERSE_EXAMPLE_SURFACES_WINDOW_CATALOG_ANDROID_PERSISTENCE_H_

#include "aether-objects/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"

#include "window_catalog_model.h"

namespace apptraverse::example::surfaces {

int PersistWindowCatalogEmbeddedUserOverlayFromDomain(
    ae::Domain& domain, DirectoryDomainStorage& user_storage);

}  // namespace apptraverse::example::surfaces

#endif  // APPTRAVERSE_EXAMPLE_SURFACES_WINDOW_CATALOG_ANDROID_PERSISTENCE_H_
