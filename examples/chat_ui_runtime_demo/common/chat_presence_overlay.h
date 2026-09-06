#ifndef CHAT_PRESENCE_OVERLAY_H_
#define CHAT_PRESENCE_OVERLAY_H_

#include <cstddef>
#include <string>

#include "chat_presence.h"

namespace chat {

class ChatRoom;

// Local-only runtime Presence cache for the local Aether identity. Journaled
// PresenceChangedEvent remains the source of truth.
class ChatPresenceOverlay {
 public:
  void Clear() { local_self_ = PresenceState::kUnknown; }

  bool SetLocalSelf(PresenceState state) {
    if (local_self_ == state) {
      return false;
    }
    local_self_ = state;
    return true;
  }

  PresenceState LocalSelf() const { return local_self_; }

  // Reseeds local ChatClient via PresenceChangedEvent Commit.
  std::size_t ApplyToRoom(ChatRoom& room,
                          std::string const& local_aether_uid) const;

 private:
  PresenceState local_self_{PresenceState::kUnknown};
};

}  // namespace chat

#endif  // CHAT_PRESENCE_OVERLAY_H_
