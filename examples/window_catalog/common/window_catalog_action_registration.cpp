#include "window_catalog_actions.h"

#include "apptraverse/object_macros.h"

namespace apptraverse::example::surfaces {

// Authoring the optional action's branch can reference its handler without
// registering an unused factory in native-menu producers.
APPTRAVERSE_REGISTER(CatalogLanguageButton);

void EnsureCatalogActionRegistration() {
  EnsureCatalogWindowActionRegistration();
  (void)&g_apptraverse_registrar_CatalogLanguageButton;
}

}  // namespace apptraverse::example::surfaces
