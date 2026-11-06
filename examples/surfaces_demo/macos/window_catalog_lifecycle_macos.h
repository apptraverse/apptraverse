#ifndef APPTRAVERSE_WINDOW_CATALOG_LIFECYCLE_MACOS_H_
#define APPTRAVERSE_WINDOW_CATALOG_LIFECYCLE_MACOS_H_

#include <filesystem>

#include "aether-objects/obj/obj_id.h"

#include "apptraverse/application_model_runtime.h"

namespace apptraverse {

int RunMacWindowCatalogModel(
    std::filesystem::path const& state_dir, ApplicationModelRuntime& runtime,
    ApplicationPublishedCallback on_published,
    ae::ObjId* pending_focus_for_ui = nullptr);

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_CATALOG_LIFECYCLE_MACOS_H_
