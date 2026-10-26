#include "apptraverse/application_model_runtime.h"

#include <optional>

#include "apptraverse/node.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/runtime_node.h"

namespace apptraverse {

void ApplicationModelRuntime::Post(ModelWork work) {
  {
    std::lock_guard<std::mutex> lock{mu_};
    if (stop_) {
      return;
    }
    pending_work_.push_back(std::move(work));
  }
  cv_.notify_all();
}

void ApplicationModelRuntime::RequestStop() {
  {
    std::lock_guard<std::mutex> lock{mu_};
    stop_ = true;
  }
  cv_.notify_all();
}

void ApplicationModelRuntime::Run(
    ae::Domain& domain, Node& application_root,
    ApplicationPublishedCallback on_published,
    BeforeIncrementalPublicationCallback before_incremental) {
  PendingDirtyNodes pending_dirty;
  BindReachableNodesMaterializedChangeNotifier(
      application_root, &pending_dirty, &PendingDirtyNodesNotify);

  auto* buffer = channel_.AcquireProducer();
  SerializeInitialPublication(application_root, buffer->sink);
  {
    std::lock_guard<std::mutex> lock{mu_};
    channel_.NotePublished();
    channel_.PublishProducer();
  }
  cv_.notify_all();
  on_published(ApplicationPublicationKind::Initial);

  for (;;) {
    std::optional<ModelWork> work;
    bool draining = false;
    {
      std::unique_lock<std::mutex> lock{mu_};
      cv_.wait(lock, [&] {
        if (stop_) {
          return true;
        }
        if (!pending_work_.empty()) {
          return true;
        }
        return !pending_dirty.empty() && !channel_.is_publication_busy();
      });
      if (stop_) {
        if (pending_work_.empty()) {
          break;
        }
        work = std::move(pending_work_.front());
        pending_work_.pop_front();
        draining = true;
      } else if (!pending_work_.empty()) {
        work = std::move(pending_work_.front());
        pending_work_.pop_front();
      }
    }

    if (work) {
      (*work)(domain);
    }

    if (draining) {
      continue;
    }

    bool publish = false;
    {
      std::lock_guard<std::mutex> lock{mu_};
      if (!stop_ && !pending_dirty.empty() && !channel_.is_publication_busy()) {
        publish = true;
      }
    }
    if (publish) {
      auto* pub = channel_.AcquireProducer();
      SerializeStructuralNodePublication(application_root, pub->sink);
      {
        std::lock_guard<std::mutex> lock{mu_};
        if (before_incremental) {
          before_incremental();
        }
        pending_dirty.Clear();
        channel_.NotePublished();
        channel_.PublishProducer();
      }
      cv_.notify_all();
      on_published(ApplicationPublicationKind::Incremental);
    }
  }

  ClearLiveReachableNodesMaterializedChangeNotifier(application_root);
}

std::vector<std::uint8_t> ApplicationModelRuntime::TakePublishedCopy(
    TakePublishedSidecarCallback sidecar) {
  std::lock_guard<std::mutex> lock{mu_};
  if (sidecar) {
    sidecar();
  }
  return channel_.TakePublishedCopy();
}

void ApplicationModelRuntime::NotifyPublicationConsumed() {
  cv_.notify_all();
}

std::uint64_t ApplicationModelRuntime::publish_count() const {
  return channel_.publish_count();
}

}  // namespace apptraverse
