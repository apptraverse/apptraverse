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

#include "dynamic_ids.h"
#include "dynamic_model.h"

namespace apptraverse {
namespace {

void EnsureItemListWindowLink(Application& application) {
  auto& window = *application.main_window;
  auto& list = *window.item_list;
  if (!list.window.is_valid()) {
    list.window = application.main_window;
  }
}

}  // namespace

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

  for (auto const& item : list->items) {
    if (item->presenter.is_valid() && item->presenter.is_loaded() &&
        item->presenter->presentation_loaded) {
      item->presenter->OnModelChanged();
    }
  }
  if (list->presenter.is_valid() && list->presenter.is_loaded() &&
      list->presenter->presentation_loaded) {
    list->presenter->OnModelChanged();
  }
  if (ui_application.main_window->add_item.is_valid() &&
      ui_application.main_window->add_item->presenter.is_valid() &&
      ui_application.main_window->add_item->presenter.is_loaded() &&
      ui_application.main_window->add_item->presenter->presentation_loaded) {
    ui_application.main_window->add_item->presenter->OnModelChanged();
  }
  if (ui_application.main_window->presenter.is_valid() &&
      ui_application.main_window->presenter.is_loaded() &&
      ui_application.main_window->presenter->presentation_loaded) {
    ui_application.main_window->presenter->OnModelChanged();
  }
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
    EnsureItemListWindowLink(*application);

    std::unordered_set<Node*> dirty_nodes;
    Node::SetMaterializedChangeNotifier(
        [&](Node& node) { dirty_nodes.insert(&node); });

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
          return !pending_work.empty() && !channel.has_unread_published();
        });
        if (stop) {
          if (pending_work.empty()) {
            break;
          }
          work = std::move(pending_work.front());
          pending_work.pop_front();
          draining = true;
        } else {
          work = std::move(pending_work.front());
          pending_work.pop_front();
        }
      }

      dirty_nodes.clear();
      (*work)(domain);

      if (draining || dirty_nodes.empty()) {
        continue;
      }

      std::vector<Node*> to_publish(dirty_nodes.begin(), dirty_nodes.end());
      for (std::size_t i = 0; i < to_publish.size(); ++i) {
        if (i > 0) {
          std::unique_lock<std::mutex> lock{mu};
          cv.wait(lock, [&] {
            return stop || !channel.has_unread_published();
          });
          if (stop) {
            // Remaining dirty pubs skipped; Domain already has committed state.
            break;
          }
        }
        auto* pub = channel.AcquireProducer();
        SerializeStructuralNodePublication(*to_publish[i], pub->sink);
        {
          std::lock_guard<std::mutex> lock{mu};
          channel.NotePublished();
          channel.PublishProducer();
        }
        cv.notify_all();
        on_published(PublicationKind::Incremental);
      }
    }

    Node::SetMaterializedChangeNotifier({});
    application.Save();
  }
}

}  // namespace apptraverse
