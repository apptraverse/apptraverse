#include "window_catalog_lifecycle.h"

#ifdef APPTRAVERSE_ENABLE_DISTILLATION
#include <filesystem>
#endif

#include <optional>
#include <unordered_set>
#include <vector>

#include "window_catalog_graph.h"
#include "window_catalog_ids.h"
#include "window_catalog_localization.h"
#include "window_catalog_model.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/node.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/runtime_node.h"

namespace apptraverse {

namespace {

ae::ObjId CatalogRootId() {
  return ae::ObjId{
      window_catalog_demo::ToObjId(window_catalog_demo::ObjId::Catalog)};
}

}  // namespace

void WindowCatalogModelSession::RequestStop() {
  {
    std::lock_guard<std::mutex> lock{mu};
    stop = true;
  }
  cv.notify_all();
}

void WindowCatalogModelSession::Post(ModelWork work) {
  {
    std::lock_guard<std::mutex> lock{mu};
    if (stop) {
      return;
    }
    pending_work.push_back(std::move(work));
  }
  cv.notify_all();
}

void ApplyWindowCatalogStructural(std::vector<std::uint8_t> const& bytes,
                                  WindowCatalog& ui_catalog,
                                  ae::IDomainStorage& ui_storage, void* host,
                                  ModelObjectProxy* model_proxy) {
  ByteSource in;
  in.data = bytes.data();
  in.size = bytes.size();
  ApplyStructuralPublicationAndUpdatePresenters(in, *ui_catalog.domain,
                                                ui_storage, ui_catalog, host,
                                                model_proxy);
}

void WindowCatalogModelSession::Run(
    std::function<void(WindowCatalogPublicationKind)> on_published) {
  EnsureObjectRegistration();
  EnsureWindowCatalogGraphRegistration();

#ifdef APPTRAVERSE_ENABLE_DISTILLATION
  bool const profile_absent = !std::filesystem::exists(state_dir) ||
                              std::filesystem::is_empty(state_dir);
  if (profile_absent) {
    DirectoryDomainStorage bootstrap_storage{state_dir};
    ae::Domain bootstrap_domain{bootstrap_storage};
    auto bootstrap_catalog = BuildWindowCatalogGraph(bootstrap_domain);
    FinalizeDistilledGraph(*bootstrap_catalog);
    SaveDistilledRoot(*bootstrap_catalog);
  }
#endif

  {
    DirectoryDomainStorage durable_storage{state_dir};
    ae::Domain domain{durable_storage};
    auto catalog = LoadApplication<WindowCatalog>(domain, CatalogRootId());
    EnsureCatalogButtonCatalogLinks(*catalog);
    EnsureWindowCatalogControlLinks(*catalog);
    RepairWindowCatalogEmbeddedStrings(*catalog);
    int const language =
        catalog->locale ? catalog->locale->language_index() : 0;
    for (auto const& row : catalog->list->rows) {
      if (!row) {
        continue;
      }
      auto* holder = static_cast<WindowHolder*>(&*row);
      if (holder->primary_text.is_valid() && !holder->primary_text.is_loaded()) {
        holder->primary_text.Load();
      }
      catalog->MaterializeHolderListText(*holder, language);
      EnsureHolderWindowTitleLinks(*holder, *catalog);
    }
    catalog->RefreshChromeButtonEnabledState();
    RestoreLoadedCatalogWindows(*catalog);
    if (catalog->list) {
      if (!catalog->list.is_loaded()) {
        catalog->list.Load();
      }
      for (auto const& row : catalog->list->rows) {
        if (row.is_valid() && !row.is_loaded()) {
          row.Load();
        }
        if (!row) {
          continue;
        }
        auto* holder = static_cast<WindowHolder*>(&*row);
        if (holder->primary_text.is_valid() && !holder->primary_text.is_loaded()) {
          holder->primary_text.Load();
        }
      }
    }

    PendingDirtyNodes pending_dirty;
    BindReachableNodesMaterializedChangeNotifier(
        *catalog, &pending_dirty, &PendingDirtyNodesNotify);

    auto* buffer = channel.AcquireProducer();
    SerializeInitialPublication(*catalog, buffer->sink);
    {
      std::lock_guard<std::mutex> lock{mu};
      channel.NotePublished();
      channel.PublishProducer();
    }
    cv.notify_all();
    on_published(WindowCatalogPublicationKind::Initial);

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

      bool publish_catalog = false;
      {
        std::lock_guard<std::mutex> lock{mu};
        if (!stop && !pending_dirty.empty() && !channel.is_publication_busy()) {
          publish_catalog = true;
        }
      }
      if (publish_catalog) {
        auto* pub = channel.AcquireProducer();
        SerializeStructuralNodePublication(*catalog, pub->sink);
        {
          std::lock_guard<std::mutex> lock{mu};
          pending_focus_for_ui = catalog->pending_focus_window_id;
          catalog->pending_focus_window_id = 0;
          pending_dirty.Clear();
          channel.NotePublished();
          channel.PublishProducer();
        }
        cv.notify_all();
        on_published(WindowCatalogPublicationKind::Incremental);
      }
    }

    ClearLiveReachableNodesMaterializedChangeNotifier(*catalog);

    durable_storage.BeginSaveSession();
    SaveWindowCatalogPersistentSnapshot(*catalog);
    if (!SavePersistentModelSnapshotToStorage(*catalog, durable_storage)) {
      shutdown_exit_code = 1;
    }
    if (durable_storage.SaveSessionHadFailure()) {
      shutdown_exit_code = 1;
    }
  }
}

}  // namespace apptraverse
