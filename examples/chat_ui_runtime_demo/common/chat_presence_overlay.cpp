#include "chat_presence_overlay.h"

#include "chat_commands.h"
#include "chat_model.h"

namespace apptraverse {

std::size_t ChatPresenceOverlay::ApplyToRoom(
    ChatRoom& room, std::string const& local_aether_uid) const {
  std::size_t changed = 0;
  if (local_aether_uid.empty()) {
    return changed;
  }
  // Overlay is cache only; reseed local ChatClient via LocalPresenceEvent.
  if (auto self = room.FindClientByAetherUid(local_aether_uid);
      self.is_valid()) {
    if (ApplyLocalPresenceEvent(*self, local_self_)) {
      ++changed;
    }
  }
  return changed;
}

}  // namespace apptraverse
