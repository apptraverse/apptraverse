#ifndef APPTRAVERSE_WINDOW_CATALOG_LIFECYCLE_H_
#define APPTRAVERSE_WINDOW_CATALOG_LIFECYCLE_H_

#include <filesystem>
#include <functional>

#include "aether-objects/obj/obj_id.h"

#include "apptraverse/application_model_runtime.h"

#include "window_catalog_main_native_frame_queue.h"

namespace apptraverse {

class WindowCatalog;

struct WindowCatalogModelSession {
  std::filesystem::path state_dir;
  std::filesystem::path local_seed_dir;
  ApplicationModelRuntime runtime;
  std::function<void(WindowCatalog&)> prepare_local_state;
  WindowCatalogMainNativeFrameQueue main_native_frame_queue;
  ae::ObjId pending_focus_for_ui{};

  int Run(ApplicationPublishedCallback on_published);
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_CATALOG_LIFECYCLE_H_
