#ifndef APPTRAVERSE_WINDOW_CATALOG_LIFECYCLE_H_
#define APPTRAVERSE_WINDOW_CATALOG_LIFECYCLE_H_

#include <filesystem>

#include "aether-objects/obj/obj_id.h"

#include "apptraverse/application_model_runtime.h"

#include "window_catalog_main_native_frame_queue.h"

namespace apptraverse {

struct WindowCatalogModelSession {
  std::filesystem::path state_dir;
  ApplicationModelRuntime runtime;
  WindowCatalogMainNativeFrameQueue main_native_frame_queue;
  ae::ObjId pending_focus_for_ui{};
  ae::ObjId pending_mobile_active_for_ui{};

  int Run(ApplicationPublishedCallback on_published);
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_CATALOG_LIFECYCLE_H_
