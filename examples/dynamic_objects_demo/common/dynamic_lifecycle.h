#ifndef APPTRAVERSE_DYNAMIC_LIFECYCLE_H_
#define APPTRAVERSE_DYNAMIC_LIFECYCLE_H_

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <variant>
#include <vector>

#include "aether-objects/obj/idomain_storage.h"
#include "aether-objects/obj/obj_id.h"

#include "apptraverse/publication_channel.h"

namespace apptraverse {

class Application;
class ItemList;

// Discrete commands. Not coalesced: each entry is one Event.
struct AddItemCommand {
  std::uint64_t sequence{0};
};

struct RemoveItemCommand {
  ae::ObjId item_id;
};

using ItemListCommand = std::variant<AddItemCommand, RemoveItemCommand>;

enum class PublicationKind {
  Initial,
  Incremental,
};

struct DynamicModelSession {
  std::filesystem::path state_dir;
  PublicationChannel<3> channel;
  std::mutex mu;
  std::condition_variable cv;
  bool stop{false};
  std::deque<ItemListCommand> pending_commands;

  void RequestStop();
  void SubmitAddItem(AddItemCommand command);
  void SubmitRemoveItem(RemoveItemCommand command);
  void Run(std::function<void(PublicationKind)> on_published);
};

// Apply structural ItemList publication, unload removed presenters, activate
// newly live ones.
void ApplyItemListStructural(std::vector<std::uint8_t> const& bytes,
                             Application& ui_application,
                             ae::IDomainStorage& ui_storage, void* host);

}  // namespace apptraverse

#endif  // APPTRAVERSE_DYNAMIC_LIFECYCLE_H_
