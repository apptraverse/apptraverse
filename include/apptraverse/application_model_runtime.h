#ifndef APPTRAVERSE_APPLICATION_MODEL_RUNTIME_H_
#define APPTRAVERSE_APPLICATION_MODEL_RUNTIME_H_

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <vector>

#include "aether-objects/obj/domain.h"

#include "apptraverse/model_object_proxy.h"
#include "apptraverse/publication_channel.h"

namespace apptraverse {

class Node;

enum class ApplicationPublicationKind {
  Initial,
  Incremental,
};

// Invoked on the model thread after a publication snapshot is committed to the
// channel, outside the publication mutex.
using ApplicationPublishedCallback =
    std::function<void(ApplicationPublicationKind kind)>;

// Invoked on the model thread under the publication mutex immediately before
// PublishProducer for an incremental snapshot (sidecar data for the consumer).
using BeforeIncrementalPublicationCallback = std::function<void()>;

// Runs under the publication mutex while taking bytes for the UI consumer.
using TakePublishedSidecarCallback = std::function<void()>;

class ApplicationModelRuntime {
 public:
  using ModelWork = ModelObjectProxy::ModelWork;

  void Post(ModelWork work);
  void RequestStop();

  // Blocks on the calling thread until RequestStop and the work queue drain.
  // application_root must already be loaded and prepared by the caller.
  void Run(ae::Domain& domain, Node& application_root,
           ApplicationPublishedCallback on_published,
           BeforeIncrementalPublicationCallback before_incremental = {});

  std::vector<std::uint8_t> TakePublishedCopy(
      TakePublishedSidecarCallback sidecar = {});

  void NotifyPublicationConsumed();

  [[nodiscard]] std::uint64_t publish_count() const;

 private:
  PublicationChannel<3> channel_;
  std::mutex mu_;
  std::condition_variable cv_;
  bool stop_{false};
  std::deque<ModelWork> pending_work_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_APPLICATION_MODEL_RUNTIME_H_
