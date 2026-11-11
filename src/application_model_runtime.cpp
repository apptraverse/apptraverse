#include "apptraverse/application_model_runtime.h"


#include <condition_variable>
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

void ApplicationModelRuntime::SyncPost(ModelWork work) {
  std::mutex done_mu;
  std::condition_variable done_cv;
  bool ran = false;
  {
    std::lock_guard<std::mutex> lock{mu_};
    if (stop_) {
      return;
    }
    // Acceptance and stop must share one lock. Once accepted, Run drains this
    // work even after RequestStop; its caller must wait for actual completion.
    pending_work_.push_back([&, work = std::move(work)](ae::Domain& domain) {
      work(domain);
      std::lock_guard<std::mutex> done_lock{done_mu};
      ran = true;
      done_cv.notify_one();
    });
  }
  cv_.notify_all();
  std::unique_lock<std::mutex> lock{done_mu};
  done_cv.wait(lock, [&] { return ran; });
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
  {
    // Compaction can release objects retained only by old journal records.
    // Keep the initial collection alive until the binding pass finishes.
    std::vector<ae::Ptr<ae::Obj>> nodes;
    CollectReachableNodeRefs(application_root, nodes);
    for (auto const& held : nodes) {
      auto* node = static_cast<Node*>(held.get());
      node->SetImmediateEventCompaction(true);
      pending_dirty.Subscribe(*node);
    }
  }

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
        if (!pending_work_.empty()) {
          return true;
        }
        if (stop_ && !channel_.is_publication_busy()) {
          return true;
        }
        if (!stop_ && !pending_dirty.empty() && !channel_.is_publication_busy()) {
          return true;
        }
        return false;
      });
      if (stop_) {
        if (pending_work_.empty()) {
          if (channel_.is_publication_busy()) {
            continue;
          }
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

  pending_dirty.ClearAllSubscribedNotifiers(domain);
}

std::vector<std::uint8_t> ApplicationModelRuntime::TakePublishedCopy(
    TakePublishedSidecarCallback sidecar) {
  std::lock_guard<std::mutex> lock{mu_};
  if (sidecar) {
    sidecar();
  }
  return channel_.TakePublishedCopy();
}

std::vector<std::uint8_t> ApplicationModelRuntime::TakePublishedCopyForUiApply(
    TakePublishedSidecarCallback sidecar) {
  std::lock_guard<std::mutex> lock{mu_};
  if (sidecar) {
    sidecar();
  }
  return channel_.TakePublishedCopyHoldUiSlot();
}

void ApplicationModelRuntime::NotifyPublicationConsumed() {
  {
    std::lock_guard<std::mutex> lock{mu_};
    channel_.ReleaseConsumer();
  }
  cv_.notify_all();
}

bool ApplicationModelRuntime::IsPublicationBusy() const {
  std::lock_guard<std::mutex> lock{mu_};
  return channel_.is_publication_busy();
}

void ApplicationModelRuntime::AbandonUnconsumedPublicationsForTeardown() {
  std::lock_guard<std::mutex> lock{mu_};
  channel_.AbandonUnconsumedSnapshots();
  cv_.notify_all();
}

std::uint64_t ApplicationModelRuntime::publish_count() const {
  return channel_.publish_count();
}

}  // namespace apptraverse
