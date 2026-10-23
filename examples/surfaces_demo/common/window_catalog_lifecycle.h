#ifndef APPTRAVERSE_WINDOW_CATALOG_LIFECYCLE_H_
#define APPTRAVERSE_WINDOW_CATALOG_LIFECYCLE_H_

#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>

#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/idomain_storage.h"

#include "apptraverse/model_object_proxy.h"
#include "apptraverse/publication_channel.h"

namespace apptraverse {

class WindowCatalog;

enum class WindowCatalogPublicationKind {
  Initial,
  Incremental,
};

struct WindowCatalogModelSession {
  using ModelWork = ModelObjectProxy::ModelWork;

  std::filesystem::path state_dir;
  PublicationChannel<3> channel;
  std::mutex mu;
  std::condition_variable cv;
  bool stop{false};
  int shutdown_exit_code{0};
  std::deque<ModelWork> pending_work;

  void RequestStop();
  void Post(ModelWork work);
  void Run(std::function<void(WindowCatalogPublicationKind)> on_published);
};

void ApplyWindowCatalogStructural(std::vector<std::uint8_t> const& bytes,
                                  WindowCatalog& ui_catalog,
                                  ae::IDomainStorage& ui_storage, void* host,
                                  ModelObjectProxy* model_proxy = nullptr);

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_CATALOG_LIFECYCLE_H_
