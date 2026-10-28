#ifndef APPTRAVERSE_WINDOW_CATALOG_LIFECYCLE_H_
#define APPTRAVERSE_WINDOW_CATALOG_LIFECYCLE_H_

#include <filesystem>

#include "aether-objects/obj/obj_id.h"

#include "apptraverse/application_model_runtime.h"

namespace apptraverse {

struct WindowCatalogModelSession {
  std::filesystem::path state_dir;
  ApplicationModelRuntime runtime;
  ae::ObjId pending_focus_for_ui{};

  int Run(ApplicationPublishedCallback on_published);
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_CATALOG_LIFECYCLE_H_
