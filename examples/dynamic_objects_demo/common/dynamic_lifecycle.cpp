#include "dynamic_lifecycle.h"

#ifdef APPTRAVERSE_ENABLE_DISTILLATION
#include <filesystem>

#include "dynamic_distill.h"
#endif

#include <cassert>
#include <optional>
#include <variant>

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

void EnsureItemListWindowLink(Application& application) {
  auto& window = *application.main_window;
  auto& list = *window.item_list;
  if (!list.window.is_valid()) {
    // Migrate ItemList v0 saves that predate the explicit parent link.
    list.window = application.main_window;
  }
}

bool ApplyItemListCommand(ItemList& list, ItemListCommand const& command) {
  if (std::get_if<AddItemCommand>(&command) != nullptr) {
    CommitAddItem(list);
    return true;
  }
  if (auto const* remove = std::get_if<RemoveItemCommand>(&command)) {
    return CommitRemoveItem(list, remove->item_id);
  }
  return false;
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
    if (stop) {
      return;
    }
    pending_commands.push_back(command);
  }
  cv.notify_all();
}

void DynamicModelSession::SubmitRemoveItem(RemoveItemCommand command) {
  {
    std::lock_guard<std::mutex> lock{mu};
    if (stop) {
      return;
    }
    pending_commands.push_back(command);
  }
  cv.notify_all();
}

void ApplyItemListStructural(std::vector<std::uint8_t> const& bytes,
                             Application& ui_application,
                             ae::IDomainStorage& ui_storage, void* host) {
  auto list = ui_application.main_window->item_list;
  ByteSource in;
  in.data = bytes.data();
  in.size = bytes.size();
  ApplyStructuralPublicationAndUpdatePresenters(
      in, *ui_application.domain, ui_storage, ui_application, host);

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
      std::optional<ItemListCommand> command;
      bool draining = false;
      {
        std::unique_lock<std::mutex> lock{mu};
        cv.wait(lock, [&] {
          if (stop) {
            return true;
          }
          return !pending_commands.empty() && !channel.has_unread_published();
        });
        if (stop) {
          if (pending_commands.empty()) {
            break;
          }
          // Accepted before RequestStop: commit for Save without waiting on
          // GUI publication consumption.
          command = pending_commands.front();
          pending_commands.pop_front();
          draining = true;
        } else {
          command = pending_commands.front();
          pending_commands.pop_front();
        }
      }

      ItemList& list = *application->main_window->item_list;
      bool const mutated = ApplyItemListCommand(list, *command);
      if (!mutated || draining) {
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
