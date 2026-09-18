#include "messenger_lifecycle.h"

#ifdef APPTRAVERSE_ENABLE_DISTILLATION
#include <filesystem>

#include "messenger_distill.h"
#endif

#include <cassert>
#include <optional>
#include <vector>

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/runtime_node.h"

#include "messenger_ids.h"
#include "messenger_model.h"

namespace apptraverse {

void MessengerModelSession::RequestStop() {
  {
    std::lock_guard<std::mutex> lock{mu};
    stop = true;
  }
  cv.notify_all();
}

void MessengerModelSession::Post(ModelWork work) {
  {
    std::lock_guard<std::mutex> lock{mu};
    if (stop) {
      return;
    }
    pending_work.push_back(std::move(work));
  }
  cv.notify_all();
}

void ApplyMessengerStructural(std::vector<std::uint8_t> const& bytes,
                              Application& ui_application,
                              ae::IDomainStorage& ui_storage, void* host,
                              ModelObjectProxy* model_proxy) {
  auto surfaces = ui_application.surfaces;
  ByteSource in;
  in.data = bytes.data();
  in.size = bytes.size();
  ApplyStructuralPublicationAndUpdatePresenters(
      in, *ui_application.domain, ui_storage, ui_application, host,
      model_proxy);

  for (auto const& surface : surfaces->surfaces) {
    surface->presenter->OnModelChanged();
  }
}

void MessengerModelSession::Run(
    std::function<void(MessengerPublicationKind)> on_published) {
#ifdef APPTRAVERSE_ENABLE_DISTILLATION
  bool state_missing = true;
  if (std::filesystem::exists(state_dir)) {
    DirectoryDomainStorage probe{state_dir};
    state_missing =
        probe
            .Enumerate(ae::ObjId{messenger::ToObjId(
                messenger::ObjId::Application)})
            .empty();
  }
  if (state_missing) {
    DirectoryDomainStorage bootstrap_storage{state_dir};
    ae::Domain bootstrap_domain{bootstrap_storage};
    auto bootstrap_app = BuildMessengerGraph(bootstrap_domain);
    FinalizeDistilledGraph(*bootstrap_app);
    SaveDistilledRoot(*bootstrap_app);
  }
#endif

  {
    DirectoryDomainStorage storage{state_dir};
    ae::Domain domain{storage};
    auto application = LoadApplication<Application>(
        domain, ae::ObjId{messenger::ToObjId(
                    messenger::ObjId::Application)});

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
    on_published(MessengerPublicationKind::Initial);

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

      // Publish at most one structural snapshot while the channel is free.
      // Further dirty Nodes stay pending (first-dirty order) until GUI consumes.
      for (;;) {
        std::uint32_t pending_id = 0;
        {
          std::lock_guard<std::mutex> lock{mu};
          if (stop || channel.is_publication_busy() || pending_dirty.empty()) {
            break;
          }
          pending_id = pending_dirty.PopFront();
        }
        Node* const to_publish = FindLiveReachableNode(*application,
                                                       pending_id);
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
        on_published(MessengerPublicationKind::Incremental);
      }
    }

    ClearReachableNodesMaterializedChangeNotifier(*application);
    application.Save();
  }
}

}  // namespace apptraverse
