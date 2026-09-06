#ifndef APPTRAVERSE_CHAT_PRESENCE_OVERLAY_H_
#define APPTRAVERSE_CHAT_PRESENCE_OVERLAY_H_

#include <cstddef>
#include <string>

#include "chat_presence.h"

namespace apptraverse {

class ChatRoom;

// Local-only runtime Presence registry for the local Aether identity.
// Shared journal replay must not become the source of Presence.
class ChatPresenceOverlay {
 public:
  void Clear() { local_self_ = PresenceState::kUnknown; }

  // Returns true only when the stored value changes.
  bool SetLocalSelf(PresenceState state) {
    if (local_self_ == state) {
      return false;
    }
    local_self_ = state;
    return true;
  }

  PresenceState LocalSelf() const { return local_self_; }

  // Reseeds local ChatClient via LocalPresenceEvent (cache → Event apply).
  // Returns 1 when Presence changed, else 0.
  std::size_t ApplyToRoom(ChatRoom& room,
                          std::string const& local_aether_uid) const;

 private:
  PresenceState local_self_{PresenceState::kUnknown};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_CHAT_PRESENCE_OVERLAY_H_
