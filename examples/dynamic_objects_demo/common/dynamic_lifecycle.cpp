#include "dynamic_lifecycle.h"

#ifdef APPTRAVERSE_ENABLE_DISTILLATION
#include <filesystem>

#include "dynamic_distill.h"
#endif

#include <cassert>
#include <optional>
#include <vector>

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/presenter.h"
#include "apptraverse/runtime_node.h"

#include "dynamic_ids.h"
#include "dynamic_model.h"

namespace apptraverse {

void DynamicModelSession::RequestStop() {
  {
    std::lock_guard<std::mutex> lock{mu};
    stop = true;
  }
  cv.notify_all();
}

void DynamicModelSession::Post(ModelWork work) {
  {
    std::lock_guard<std::mutex> lock{mu};
    if (stop) {
      return;
    }
    pending_work.push_back(std::move(work));
  }
  cv.notify_all();
}

void ApplyItemListStructural(std::vector<std::uint8_t> const& bytes,
                             Application& ui_application,
                             ae::IDomainStorage& ui_storage, void* host,
                             ModelObjectProxy* model_proxy) {
  auto list = ui_application.main_window->item_list;
  ByteSource in;
  in.data = bytes.data();
  in.size = bytes.size();
  ApplyStructuralPublicationAndUpdatePresenters(
      in, *ui_application.domain, ui_storage, ui_application, host,
      model_proxy);

  // Live presenters are already presentation_loaded after structural apply.
  for (auto const& item : list->items) {
    item->presenter->OnModelChanged();
  }
  list->presenter->OnModelChanged();
  ui_application.main_window->add_item->presenter->OnModelChanged();
  ui_application.main_window->presenter->OnModelChanged();
}

void DynamicModelSession::Run(
    std::function<void(PublicationKind)> on_published) {
#ifdef APPTRAVERSE_ENABLE_DISTILLATION
  bool state_missing = true;
  if (std::filesystem::exists(state_dir)) {
    DirectoryDomainStorage probe{state_dir};
    state_missing =
        probe
            .Enumerate(ae::ObjId{dynamic_objects::ToObjId(
                dynamic_objects::ObjId::Application)})
            .empty();
  }
  if (state_missing) {
    DirectoryDomainStorage bootstrap_storage{state_dir};
    ae::Domain bootstrap_domain{bootstrap_storage};
    auto bootstrap_app = BuildDynamicObjectsGraph(bootstrap_domain);
    FinalizeDistilledGraph(*bootstrap_app);
    SaveDistilledRoot(*bootstrap_app);
  }
#endif

  {
    DirectoryDomainStorage storage{state_dir};
    ae::Domain domain{storage};
    auto application = LoadApplication<Application>(
        domain, ae::ObjId{dynamic_objects::ToObjId(
                    dynamic_objects::ObjId::Application)});
    // ItemList::window is schema v1. Pre-v1 state is not repaired at runtime;
    // re-distill / fresh state is required.

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
    on_published(PublicationKind::Initial);

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
        (*work)(domain);
      }

      if (draining) {
        continue;
      }

      for (;;) {
        Node* to_publish = nullptr;
        {
          std::lock_guard<std::mutex> lock{mu};
          if (stop || channel.is_publication_busy() || pending_dirty.empty()) {
            break;
          }
          to_publish = pending_dirty.PopFront();
        }
        auto* pub = channel.AcquireProducer();
        SerializeStructuralNodePublication(*to_publish, pub->sink);
        {
          std::lock_guard<std::mutex> lock{mu};
          channel.NotePublished();
          channel.PublishProducer();
        }
        cv.notify_all();
        on_published(PublicationKind::Incremental);
      }
    }

    ClearReachableNodesMaterializedChangeNotifier(*application);
    application.Save();
  }
}

}  // namespace apptraverse
