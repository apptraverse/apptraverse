#ifndef APPTRAVERSE_WINDOW_CATALOG_LIFECYCLE_H_
#define APPTRAVERSE_WINDOW_CATALOG_LIFECYCLE_H_

#include <filesystem>
#include <functional>

#include "aether-objects/obj/obj_id.h"

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/model_object_proxy.h"

namespace apptraverse {

enum class WindowCatalogPublicationKind {
  Initial,
  Incremental,
};

struct WindowCatalogModelSession {
  using ModelWork = ModelObjectProxy::ModelWork;

  std::filesystem::path state_dir;
  ApplicationModelRuntime runtime;
  ae::ObjId pending_focus_for_ui{};

  void RequestStop() { runtime.RequestStop(); }

  void Post(ModelWork work) { runtime.Post(std::move(work)); }

  void Run(std::function<void(WindowCatalogPublicationKind)> on_published);
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_CATALOG_LIFECYCLE_H_
