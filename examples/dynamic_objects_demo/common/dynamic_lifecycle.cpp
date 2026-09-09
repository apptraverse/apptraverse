#include "dynamic_lifecycle.h"

#ifdef APPTRAVERSE_ENABLE_DISTILLATION
#include <filesystem>
#endif

#include <cassert>

#include "aether-objects/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/object_serialization.h"

#include "dynamic_ids.h"
#include "dynamic_model.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(Item);
APPTRAVERSE_REGISTER(ItemPresenter);
APPTRAVERSE_REGISTER(ItemList);
APPTRAVERSE_REGISTER(ItemListPresenter);
APPTRAVERSE_REGISTER(AddItemEvent);
APPTRAVERSE_REGISTER(MainWindow);
APPTRAVERSE_REGISTER(MainWindowPresenter);
APPTRAVERSE_REGISTER(Application);

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
    pending_adds.push_back(command);
  }
  cv.notify_all();
}

void ApplyItemListStructural(std::vector<std::uint8_t> const& bytes,
                             Application& ui_application,
                             ae::IDomainStorage& ui_storage, void* host) {
  // Hold live mirror objects across graph deserialize so existing presenters
  // (and their presentation_loaded / native state) are not released when
  // ItemList's ObjPtr fields are reloaded.
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

  ByteSource in;
  in.data = bytes.data();
  in.size = bytes.size();
  ApplyStructuralPublication(in, *ui_application.domain, ui_storage);
  assert(&*ui_application.main_window == &*window);
  assert(&*ui_application.main_window->item_list == &*list);
  assert(&*list->presenter == &*list_presenter);
  assert(&*window->presenter == &*window_presenter);
  assert(list->items.size() >= existing_items.size());
  for (std::size_t i = 0; i < existing_items.size(); ++i) {
    assert(&*list->items[i] == &*existing_items[i]);
    assert(&*list->items[i]->presenter == &*existing_item_presenters[i]);
  }

  InitializeNewPresenters(ui_application, host);
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
      AddItemCommand command;
      {
        std::unique_lock<std::mutex> lock{mu};
        cv.wait(lock, [&] {
          return stop ||
                 (!pending_adds.empty() && !channel.has_unread_published());
        });
        if (stop) {
          break;
        }
        command = pending_adds.front();
        pending_adds.pop_front();
      }
      (void)command;

      ItemList& list = *application->main_window->item_list;
      CommitAddItem(list);

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

    // ItemList keeps default unlimited retention so AddItemEvent survives
    // restart/replay. MainWindow has no resize journal here.
    application.Save();
  }
}

}  // namespace apptraverse
