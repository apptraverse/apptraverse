#include "apptraverse/application/model_publication_session.h"

#include <cassert>
#include <filesystem>
#include <optional>
#include <unordered_set>
#include <vector>

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

  DirectoryDomainStorage durable_storage{state_dir};
  if (config.bootstrap_if_missing) {
    bool state_missing =
        durable_storage.Enumerate(config.root_object_id).empty();
    if (state_missing) {
      ae::Domain bootstrap_domain{durable_storage};
      config.bootstrap_if_missing(bootstrap_domain, durable_storage);
    }
  }
  ae::IDomainStorage* storage = &durable_storage;
  if (config.select_storage) {
    storage = config.select_storage(durable_storage);
  }
  ae::Domain domain{*storage};
  ae::Obj* const root = config.load_mutable_root(domain);
  assert(root != nullptr);

  if (config.on_model_root_loaded) {
    config.on_model_root_loaded(*root);
  }

  PendingDirtyNodes pending_dirty;
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

    std::unordered_set<std::uint32_t> live_ids;
    {
      std::vector<ae::Obj*> live;
      CollectLiveReachableObjects(*root, live);
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
      on_published(ModelPublicationKind::Incremental);
    }
  }

  ClearLiveReachableNodesMaterializedChangeNotifier(*root);
  if (config.on_clear_live_notifiers) {
    config.on_clear_live_notifiers(*root);
  }

  durable_storage.BeginSaveSession();
  if (config.on_save_start) {
    config.on_save_start();
  }
  if (config.persist_root) {
    config.persist_root(*root);
  } else {
    ae::Obj::ptr::MakeFromThis(root).Save();
  }
  if (config.on_save_end) {
    config.on_save_end(*root, durable_storage);
  }
  if (durable_storage.SaveSessionHadFailure()) {
    shutdown_exit_code = 1;
  }
  model_root = ae::Obj::ptr{};
}

}  // namespace apptraverse
