#ifndef APPTRAVERSE_WINDOW_CATALOG_LIFECYCLE_H_
#define APPTRAVERSE_WINDOW_CATALOG_LIFECYCLE_H_

#include <filesystem>
#include <functional>

#include "aether-objects/obj/obj_id.h"

#include "apptraverse/application_model_runtime.h"

namespace apptraverse {

class DirectoryDomainStorage;
class WindowCatalog;

struct WindowCatalogModelSession {
  std::filesystem::path state_dir;
  ApplicationModelRuntime runtime;
  ae::ObjId pending_focus_for_ui{};

  // Installed on the model thread before the run loop. The UI thread may call
  // it to write the same snapshot as shutdown, without stopping. Completion
  // runs on the model thread.
  std::function<void(std::function<void(int status)>)> request_persist;

  WindowCatalog* live_catalog_{nullptr};
  DirectoryDomainStorage* live_storage_{nullptr};

  int Run(ApplicationPublishedCallback on_published);
  void ArmPersist(WindowCatalog& catalog, DirectoryDomainStorage& storage);
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_CATALOG_LIFECYCLE_H_
