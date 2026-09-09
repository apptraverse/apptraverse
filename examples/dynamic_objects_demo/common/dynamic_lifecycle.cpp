#include "dynamic_lifecycle.h"

#ifdef APPTRAVERSE_ENABLE_DISTILLATION
#include <filesystem>
#endif

#include <algorithm>
#include <cassert>
#include <unordered_set>
#include <vector>

#include "aether-objects/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/presenter.h"

#include "dynamic_ids.h"
#include "dynamic_model.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(Item);
APPTRAVERSE_REGISTER(ItemPresenter);
APPTRAVERSE_REGISTER(ItemList);
APPTRAVERSE_REGISTER(ItemListPresenter);
APPTRAVERSE_REGISTER(AddItemEvent);
APPTRAVERSE_REGISTER(RemoveItemEvent);
APPTRAVERSE_REGISTER(MainWindow);
APPTRAVERSE_REGISTER(MainWindowPresenter);
APPTRAVERSE_REGISTER(Application);

std::vector<Presenter*> CaptureActivePresenters(Application& ui_application) {
  std::vector<ae::Obj*> objects;
  CollectLiveReachableObjects(ui_application, objects);
  std::vector<Presenter*> active;
  for (ae::Obj* obj : objects) {
    auto* presenter = dynamic_cast<Presenter*>(obj);
    if (presenter != nullptr && presenter->presentation_loaded) {
      active.push_back(presenter);
    }
  }
  return active;
}

}  // namespace

void DynamicModelSession::RequestStop() {
  {
    std::lock_guard<std::mutex> lock{mu};
    stop = true;
  }
  cv.notify_all();
}

void DynamicModelSession::SubmitAddItem(AddItemCommand command) {
  {
    std::lock_guard<std::mutex> lock{mu};
    pending_commands.push_back(command);
  }
  cv.notify_all();
}

void DynamicModelSession::SubmitRemoveItem(RemoveItemCommand command) {
  {
    std::lock_guard<std::mutex> lock{mu};
    pending_commands.push_back(command);
  }
  cv.notify_all();
}

void ApplyItemListStructural(std::vector<std::uint8_t> const& bytes,
                             Application& ui_application,
                             ae::IDomainStorage& ui_storage, void* host) {
  // Hold live mirror objects and previously-active presenters across apply so
  // identity is preserved and OnUnload can run while presenters still exist.
  auto window = ui_application.main_window;
  auto list = window->item_list;
  auto list_presenter = list->presenter;
  auto window_presenter = window->presenter;
  std::vector<Item::ptr> existing_items = list->items;
  std::vector<ItemPresenter::ptr> existing_item_presenters;
  existing_item_presenters.reserve(existing_items.size());
  for (auto const& item : existing_items) {
    existing_item_presenters.push_back(item->presenter);
  }
  std::vector<Presenter::ptr> held_active;
  auto const previously_active = CaptureActivePresenters(ui_application);
  held_active.reserve(previously_active.size());
  for (Presenter* presenter : previously_active) {
    held_active.push_back(Presenter::ptr::MakeFromThis(presenter));
  }

  ByteSource in;
  in.data = bytes.data();
  in.size = bytes.size();
  ApplyStructuralPublication(in, *ui_application.domain, ui_storage);
  assert(&*ui_application.main_window == &*window);
  assert(&*ui_application.main_window->item_list == &*list);
  assert(&*list->presenter == &*list_presenter);
  assert(&*window->presenter == &*window_presenter);

  std::unordered_set<std::uint32_t> live_ids;
  for (auto const& item : list->items) {
    assert(item.is_valid());
    live_ids.insert(item->obj_id.id());
  }
  for (std::size_t i = 0; i < existing_items.size(); ++i) {
    auto const id = existing_items[i]->obj_id.id();
    if (live_ids.count(id) == 0) {
      continue;
    }
    auto const it = std::find_if(
        list->items.begin(), list->items.end(),
        [&](Item::ptr const& item) { return item->obj_id.id() == id; });
    assert(it != list->items.end());
    assert(&**it == &*existing_items[i]);
    assert(&*(*it)->presenter == &*existing_item_presenters[i]);
  }

  UpdatePresentersAfterStructuralPublication(ui_application, previously_active,
                                             host);

  for (auto const& item : list->items) {
    if (item->presenter.is_valid() && item->presenter.is_loaded() &&
        item->presenter->presentation_loaded) {
      item->presenter->OnModelChanged();
    }
  }
  if (list_presenter.is_valid() && list_presenter.is_loaded() &&
      list_presenter->presentation_loaded) {
    list_presenter->OnModelChanged();
  }
  if (window_presenter.is_valid() && window_presenter.is_loaded() &&
      window_presenter->presentation_loaded) {
    window_presenter->OnModelChanged();
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
      ItemListCommand command;
      {
        std::unique_lock<std::mutex> lock{mu};
        cv.wait(lock, [&] {
          return stop ||
                 (!pending_commands.empty() && !channel.has_unread_published());
        });
        if (stop) {
          break;
        }
        command = pending_commands.front();
        pending_commands.pop_front();
      }

      ItemList& list = *application->main_window->item_list;
      bool mutated = false;
      if (auto const* add = std::get_if<AddItemCommand>(&command)) {
        (void)add;
        CommitAddItem(list);
        mutated = true;
      } else if (auto const* remove = std::get_if<RemoveItemCommand>(&command)) {
        mutated = CommitRemoveItem(list, remove->item_id);
      }
      if (!mutated) {
        continue;
      }

      auto* pub = channel.AcquireProducer();
      SerializeStructuralNodePublication(list, pub->sink);
      {
        std::lock_guard<std::mutex> lock{mu};
        channel.NotePublished();
        channel.PublishProducer();
      }
      cv.notify_all();
      on_published(PublicationKind::Incremental);
    }

    // ItemList keeps default unlimited retention so topology Events survive
    // restart/replay.
    application.Save();
  }
}

}  // namespace apptraverse
