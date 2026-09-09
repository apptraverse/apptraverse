#ifndef APPTRAVERSE_DYNAMIC_LIFECYCLE_H_
#define APPTRAVERSE_DYNAMIC_LIFECYCLE_H_

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <vector>

#include "aether-objects/obj/idomain_storage.h"

#include "apptraverse/publication_channel.h"

namespace apptraverse {

class Application;
class ItemList;

// Discrete add request. Not coalesced: each entry is one AddItemEvent.
struct AddItemCommand {
  std::uint64_t sequence{0};
};

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
  std::deque<AddItemCommand> pending_adds;

  void RequestStop();
  void SubmitAddItem(AddItemCommand command);
  void Run(std::function<void(PublicationKind)> on_published);
};

// Apply structural ItemList publication and activate any new presenters.
void ApplyItemListStructural(std::vector<std::uint8_t> const& bytes,
                             Application& ui_application,
                             ae::IDomainStorage& ui_storage, void* host);

}  // namespace apptraverse

#endif  // APPTRAVERSE_DYNAMIC_LIFECYCLE_H_
