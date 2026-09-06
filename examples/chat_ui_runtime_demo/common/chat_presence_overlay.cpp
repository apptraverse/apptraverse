#include "chat_presence_overlay.h"

#include "chat_commands.h"
#include "chat_model.h"

namespace chat {

std::size_t ChatPresenceOverlay::ApplyToRoom(
    ChatRoom& room, std::string const& local_aether_uid) const {
  std::size_t changed = 0;
  if (local_aether_uid.empty()) {
    return changed;
  }
  if (auto self = room.FindClientByAetherUid(local_aether_uid);
      self.is_valid()) {
    if (CommitPresenceChanged(*self, local_self_)) {
      ++changed;
    }
  }
  return changed;
}

}  // namespace chat
