#ifndef APPTRAVERSE_WINDOW_CATALOG_LIFECYCLE_WIN_H_
#define APPTRAVERSE_WINDOW_CATALOG_LIFECYCLE_WIN_H_

#include <filesystem>

#include "window_catalog_lifecycle.h"

namespace apptraverse {

#if defined(APPTRAVERSE_ENABLE_DISTILLATION)
void BootstrapWindowCatalogProfileIfAbsent(std::filesystem::path const& state_dir);
#endif

#if defined(APPTRAVERSE_WINDOW_CATALOG_USE_EMBEDDED_DISTILL)
int RunWindowCatalogEmbeddedModelSession(
    WindowCatalogModelSession& session,
    ApplicationPublishedCallback on_published);
#endif

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_CATALOG_LIFECYCLE_WIN_H_
