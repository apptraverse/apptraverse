#ifndef APPTRAVERSE_WINDOW_CATALOG_LIFECYCLE_H_
#define APPTRAVERSE_WINDOW_CATALOG_LIFECYCLE_H_

#include <filesystem>
#include <functional>
#include <vector>

#include "aether-objects/obj/idomain_storage.h"
#include "aether-objects/obj/obj_id.h"

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/model_object_proxy.h"

namespace apptraverse {

class WindowCatalog;

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

void ApplyWindowCatalogStructural(std::vector<std::uint8_t> const& bytes,
                                  WindowCatalog& ui_catalog,
                                  ae::IDomainStorage& ui_storage, void* host,
                                  ModelObjectProxy* model_proxy = nullptr);

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_CATALOG_LIFECYCLE_H_
