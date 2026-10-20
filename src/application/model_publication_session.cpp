#include "apptraverse/application/model_publication_session.h"



#include <cassert>

#include <filesystem>

#include <optional>

#include <unordered_set>

#include <vector>



#include "aether-objects/domain_storage/ram_domain_storage.h"



#include "apptraverse/directory_domain_storage.h"

#include "apptraverse/object_serialization.h"
#include "apptraverse/runtime_node.h"

namespace apptraverse {



void ModelPublicationSession::RequestStop() {

  {

    std::lock_guard<std::mutex> lock{mu};

    stop = true;

  }

  cv.notify_all();

}



void ModelPublicationSession::Post(ModelWork work) {

  {

    std::lock_guard<std::mutex> lock{mu};

    if (stop) {

      return;

    }

    pending_work.push_back(std::move(work));

  }

  cv.notify_all();

}



void ApplyModelStructuralPublication(std::vector<std::uint8_t> const& bytes,

                                     ae::Obj& ui_root,

                                     ae::IDomainStorage& ui_storage, void* host,

                                     ModelObjectProxy* model_proxy) {

  ByteSource in;

  in.data = bytes.data();

  in.size = bytes.size();

  ApplyStructuralPublicationAndUpdatePresenters(in, *ui_root.domain, ui_storage,

                                                ui_root, host, model_proxy);

}



void ModelPublicationSession::Run(

    std::function<void(ModelPublicationKind)> on_published) {

  assert(config.ensure_type_registration);

  assert(config.load_mutable_root);

  assert(config.root_object_id.id() != 0);



  config.ensure_type_registration();

  model_root = ae::Obj::ptr{};



  presentation_size_queue.BindPostWork(

      [this](ModelObjectProxy::ModelWork work) { Post(std::move(work)); });

  if (config.bind_presentation_size_queue) {

    config.bind_presentation_size_queue(presentation_size_queue);

  }



  ae::RamDomainStorage ram_storage;

  DirectoryDomainStorage directory_storage{state_dir};

  ModelCheckpointPaths const checkpoint_paths =

      ModelCheckpointPathsForStateDir(state_dir);



  ae::IDomainStorage* storage = nullptr;

  DirectoryDomainStorage* directory_for_save = nullptr;



  if (config.durable_persistence == ModelDurablePersistence::PackedCheckpointFile) {

    if (!HasActiveModelCheckpoint(checkpoint_paths)) {

      if (config.bootstrap_if_missing) {

        ae::Domain bootstrap_domain{ram_storage};

        config.bootstrap_if_missing(bootstrap_domain, ram_storage);

      }

    }

    storage = &ram_storage;

  } else {

    if (config.bootstrap_if_missing) {

      bool state_missing =

          directory_storage.Enumerate(config.root_object_id).empty();

      if (state_missing) {

        ae::Domain bootstrap_domain{directory_storage};

        config.bootstrap_if_missing(bootstrap_domain, directory_storage);

      }

    }

    storage = &directory_storage;

    if (config.select_storage) {

      storage = config.select_storage(directory_storage);

    }

    directory_for_save = &directory_storage;

  }



  ae::Domain domain{*storage};



  ae::Obj* root = nullptr;

  bool const load_packed_checkpoint =

      config.durable_persistence == ModelDurablePersistence::PackedCheckpointFile &&

      HasActiveModelCheckpoint(checkpoint_paths);

  if (load_packed_checkpoint) {
    if (!config.create_checkpoint_root_shell) {
      shutdown_exit_code = 1;
      return;
    }
    root = config.create_checkpoint_root_shell(domain);
    if (root == nullptr) {
      shutdown_exit_code = 1;
      return;
    }
    if (!model_root) {
      model_root = ae::Obj::ptr::MakeFromThis(root);
    }

    if (!LoadModelCheckpoint(checkpoint_paths, domain, *storage, *root)) {

      shutdown_exit_code = 1;

      return;

    }

  } else {

    root = config.load_mutable_root(domain);

    if (!model_root) {

      model_root = ae::Obj::ptr::MakeFromThis(root);

    }

  }

  assert(root != nullptr);



  if (config.on_model_root_loaded) {

    config.on_model_root_loaded(*root);

  }



  pending_dirty.Clear();

  pending_dirty_count_observed.store(0);

  BindReachableNodesMaterializedChangeNotifier(*root, &pending_dirty,

                                             &PendingDirtyNodesNotify);



  auto* buffer = channel.AcquireProducer();

  SerializeInitialPublication(*root, buffer->sink);

  {

    std::lock_guard<std::mutex> lock{mu};

    channel.NotePublished();

    channel.PublishProducer();

  }

  cv.notify_all();

  on_published(ModelPublicationKind::Initial);

  auto try_publish_one_incremental = [&]() -> bool {
    struct IncrementalGuard {
      ModelPublicationSession& session;
      ~IncrementalGuard() {
        session.incremental_publication_in_progress.store(false);
      }
    };

    {
      std::lock_guard<std::mutex> lock{mu};
      if (stop || pending_dirty.empty() || channel.is_publication_busy()) {
        return false;
      }
    }

    incremental_publication_in_progress.store(true);
    IncrementalGuard guard{*this};

    if (incremental_prep_barrier.enabled) {
      std::unique_lock<std::mutex> lock{incremental_prep_barrier.mu};
      incremental_prep_barrier.at_checkpoint = true;
      incremental_prep_barrier.cv.notify_all();
      incremental_prep_barrier.cv.wait(lock, [&] {
        return incremental_prep_barrier.add_posted || stop;
      });
      incremental_prep_barrier.cv.wait(lock, [&] {
        return incremental_prep_barrier.continue_prep || stop;
      });
      incremental_prep_barrier.at_checkpoint = false;
    }

    std::uint32_t pending_id = 0;
    {
      std::lock_guard<std::mutex> lock{mu};
      if (stop || channel.is_publication_busy() || pending_dirty.empty()) {
        return false;
      }
      pending_id = pending_dirty.ordered.front();
    }

    std::unordered_set<std::uint32_t> live_ids;
    {
      std::vector<ae::Obj*> live;
      CollectLiveReachableObjects(*root, live);
      live_ids.reserve(live.size());
      for (ae::Obj* obj : live) {
        live_ids.insert(obj->obj_id.id());
      }
    }

    {
      std::lock_guard<std::mutex> lock{mu};
      if (!pending_work.empty()) {
        return false;
      }
    }

    {
      std::lock_guard<std::mutex> lock{mu};
      if (stop || channel.is_publication_busy() || pending_dirty.empty()) {
        return false;
      }
      pending_id = pending_dirty.PopFront();
      pending_dirty_count_observed.store(pending_dirty.ordered.size());
    }

    if (live_ids.count(pending_id) == 0) {
      return false;
    }

    auto object = domain.Find(ae::ObjId{pending_id});
    if (!object) {
      return false;
    }

    Node* const to_publish = object.as<Node>();
    if (to_publish == nullptr) {
      return false;
    }

    if (config.before_incremental_publication) {
      config.before_incremental_publication(pending_id);
    }

    auto* pub = channel.AcquireProducer();
    SerializeStructuralNodePublication(*to_publish, pub->sink);

    {
      std::lock_guard<std::mutex> lock{mu};
      channel.NotePublished();
      channel.PublishProducer();
    }

    model_work_executions_since_incremental_publish.store(0);
    cv.notify_all();
    on_published(ModelPublicationKind::Incremental);
    return true;
  };

  auto should_interleave_publish = [&]() -> bool {
    std::lock_guard<std::mutex> lock{mu};
    if (stop || pending_dirty.empty() || channel.is_publication_busy()) {
      return false;
    }
    if (pending_work.empty()) {
      return true;
    }
    if (pending_work.size() > 1) {
      return true;
    }
    return model_work_executions_since_incremental_publish.load() >= 2;
  };

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

          if (config.on_model_drained_accepted_tasks) {

            config.on_model_drained_accepted_tasks();

          }

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

      (*work)(domain);
      ++model_work_executions_since_incremental_publish;

    }



    if (draining) {

      continue;

    }

    pending_dirty_count_observed.store(pending_dirty.ordered.size());

    if (should_interleave_publish() && try_publish_one_incremental()) {
      continue;
    }

    // Re-enter the loop before publishing so newly posted ModelWork (e.g. Add
    // right after a long seed task) is not starved by incremental publications.
    {
      std::lock_guard<std::mutex> lock{mu};
      if (!pending_work.empty()) {
        continue;
      }
    }

    if (try_publish_one_incremental()) {
      continue;
    }

  }



  ClearLiveReachableNodesMaterializedChangeNotifier(*root);

  if (config.on_clear_live_notifiers) {

    config.on_clear_live_notifiers(*root);

  }



  ModelPublicationSaveContext save_context;

  save_context.backend = config.durable_persistence;

  save_context.checkpoint_paths = checkpoint_paths;

  save_context.directory_storage = directory_for_save;



  if (config.on_save_start) {

    config.on_save_start();

  }



  if (config.durable_persistence == ModelDurablePersistence::PackedCheckpointFile) {

    save_context.success = CommitModelCheckpoint(*root, checkpoint_paths);

    save_context.checkpoint_bytes =

        save_context.success ? ActiveModelCheckpointByteSize(checkpoint_paths) : 0;

    if (!save_context.success) {

      shutdown_exit_code = 1;

    }

  } else {

    directory_for_save->BeginSaveSession();

    if (config.persist_root) {

      config.persist_root(*root);

    } else {

      ae::Obj::ptr::MakeFromThis(root).Save();

    }

    save_context.success = !directory_for_save->SaveSessionHadFailure();

    if (!save_context.success) {

      shutdown_exit_code = 1;

    }

  }



  if (config.on_save_end) {

    config.on_save_end(*root, save_context);

  }



  model_root = ae::Obj::ptr{};

}



}  // namespace apptraverse


