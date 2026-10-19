#include "surfaces_lifecycle.h"

#include "surfaces_shutdown_trace.h"

#ifdef APPTRAVERSE_ENABLE_DISTILLATION
#include <filesystem>

#include "surfaces_distill.h"
#endif

#include <cassert>
#include <optional>
#include <unordered_set>
#include <vector>

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/node.h"
#include "apptraverse/runtime_node.h"

#include "apptraverse/surfaces/surfaces_ids.h"
#include "apptraverse/surfaces/surfaces_model.h"
#include "apptraverse/surfaces/surfaces_registration.h"

namespace apptraverse {

void SurfacesModelSession::RequestStop() {
  {
    std::lock_guard<std::mutex> lock{mu};
    stop = true;
  }
  cv.notify_all();
}

void SurfacesModelSession::Post(ModelWork work) {
  {
    std::lock_guard<std::mutex> lock{mu};
    if (stop) {
      return;
    }
    pending_work.push_back(std::move(work));
  }
  cv.notify_all();
}

void ApplySurfacesStructural(std::vector<std::uint8_t> const& bytes,
                             Application& ui_application,
                             ae::IDomainStorage& ui_storage, void* host,
                             ModelObjectProxy* model_proxy) {
  ByteSource in;
  in.data = bytes.data();
  in.size = bytes.size();
  ApplyStructuralPublicationAndUpdatePresenters(
      in, *ui_application.domain, ui_storage, ui_application, host,
      model_proxy);
}

void SurfacesModelSession::Run(
    std::function<void(SurfacesPublicationKind)> on_published) {
  EnsureSurfacesRegistration();
  presentation_size_queue.BindPostWork(
      [this](ModelObjectProxy::ModelWork work) { Post(std::move(work)); });

#ifdef APPTRAVERSE_ENABLE_DISTILLATION
  bool state_missing = true;
  if (std::filesystem::exists(state_dir)) {
    DirectoryDomainStorage probe{state_dir};
    state_missing =
        probe
            .Enumerate(ae::ObjId{surfaces_demo::ToObjId(
                surfaces_demo::ObjId::Application)})
            .empty();
  }
  if (state_missing) {
    DirectoryDomainStorage bootstrap_storage{state_dir};
    ae::Domain bootstrap_domain{bootstrap_storage};
    auto bootstrap_app = BuildSurfacesGraph(bootstrap_domain);
    FinalizeDistilledGraph(*bootstrap_app);
    SaveDistilledRoot(*bootstrap_app);
  }
#endif

  {
    DirectoryDomainStorage durable_storage{state_dir};
    std::optional<InstrumentedDomainStorage> instrumented_storage;
    ae::IDomainStorage* storage = &durable_storage;
    if (SurfacesShutdownTrace::Enabled()) {
      instrumented_storage.emplace(durable_storage, durable_storage.root());
      storage = &*instrumented_storage;
    }
    ae::Domain domain{*storage};
    auto application = LoadApplication<Application>(
        domain, ae::ObjId{surfaces_demo::ToObjId(
                    surfaces_demo::ObjId::Application)});
    PendingDirtyNodes pending_dirty;
    BindReachableNodesMaterializedChangeNotifier(
        *application, &pending_dirty, &PendingDirtyNodesNotify);

    auto* buffer = channel.AcquireProducer();
    SerializeInitialPublication(*application, buffer->sink);
    {
      std::lock_guard<std::mutex> lock{mu};
      channel.NotePublished();
      channel.PublishProducer();
    }
    cv.notify_all();
    on_published(SurfacesPublicationKind::Initial);

    for (;;) {
      std::optional<ModelWork> work;
      bool draining = false;
      {
        std::unique_lock<std::mutex> lock{mu};
        cv.wait(lock, [&] {
          if (stop) {
            return true;
          }
          if (!pending_work.empty()) {
            return true;
          }
          return !pending_dirty.empty() && !channel.is_publication_busy();
        });
        if (stop) {
          if (pending_work.empty()) {
            SurfacesShutdownTrace::MarkModelDrainedAcceptedTasks();
            break;
          }
          work = std::move(pending_work.front());
          pending_work.pop_front();
          draining = true;
        } else if (!pending_work.empty()) {
          work = std::move(pending_work.front());
          pending_work.pop_front();
        }
      }

      if (work) {
        // Unread GUI publication must not block accepted ModelWork.
        (*work)(domain);
      }

      if (draining) {
        continue;
      }

      {
        std::lock_guard<std::mutex> lock{mu};
        if (stop || pending_dirty.empty() || channel.is_publication_busy()) {
          continue;
        }
      }

      // Publish at most one structural snapshot while the channel is free.
      // Further dirty Nodes stay pending (first-dirty order) until GUI consumes.
      std::unordered_set<std::uint32_t> live_ids;
      {
        std::vector<ae::Obj*> live;
        CollectLiveReachableObjects(*application, live);
        live_ids.reserve(live.size());
        for (ae::Obj* obj : live) {
          live_ids.insert(obj->obj_id.id());
        }
      }
      for (;;) {
        std::uint32_t pending_id = 0;
        {
          std::lock_guard<std::mutex> lock{mu};
          if (stop || channel.is_publication_busy() || pending_dirty.empty()) {
            break;
          }
          pending_id = pending_dirty.PopFront();
        }
        if (live_ids.count(pending_id) == 0) {
          continue;
        }
        auto object = domain.Find(ae::ObjId{pending_id});
        if (!object) {
          continue;
        }
        Node* const to_publish = object.as<Node>();
        if (to_publish == nullptr) {
          continue;
        }
        auto* pub = channel.AcquireProducer();
        SerializeStructuralNodePublication(*to_publish, pub->sink);
        {
          std::lock_guard<std::mutex> lock{mu};
          channel.NotePublished();
          channel.PublishProducer();
        }
        cv.notify_all();
        on_published(SurfacesPublicationKind::Incremental);
      }
    }

    ClearLiveReachableNodesMaterializedChangeNotifier(*application);
    SurfacesShutdownTrace::MarkClearLiveNotifiersDone();

    struct StoreStatsGuard {
      DirectoryDomainStorageStoreStats stats{};
      StoreStatsGuard() { SetDirectoryDomainStorageStoreStats(&stats); }
      ~StoreStatsGuard() { TakeDirectoryDomainStorageStoreStats(); }
    } store_stats_guard;
    SurfacesShutdownTrace::MarkApplicationSaveStart();
    application.Save();
    SurfacesShutdownTrace::MarkApplicationSaveEnd();
    SurfacesShutdownTrace::LogStorageStoreStats(
        store_stats_guard.stats.layer_store_calls,
        store_stats_guard.stats.layers_written,
        store_stats_guard.stats.layers_skipped_unchanged,
        store_stats_guard.stats.layers_written_bytes);
    if (instrumented_storage) {
      InstrumentedDomainStorageStats const save_stats =
          instrumented_storage->SnapshotStats();
      SurfacesShutdownTrace::LogInstrumentedSaveStats(
          save_stats.store_calls, save_stats.store_bytes,
          save_stats.store_new_layers, save_stats.store_overwrite_layers,
          save_stats.store_skipped_unchanged_layers);
    }
    SurfacesShutdownTrace::MarkApplicationGraphTeardownStart();
  }
  SurfacesShutdownTrace::MarkModelDomainDestroyed();
}

}  // namespace apptraverse
