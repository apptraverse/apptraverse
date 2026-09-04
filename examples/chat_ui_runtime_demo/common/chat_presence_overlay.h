#ifndef APPTRAVERSE_CHAT_PRESENCE_OVERLAY_H_
#define APPTRAVERSE_CHAT_PRESENCE_OVERLAY_H_

#include <cstddef>
#include <string>
#include <unordered_map>

#include "chat_presence.h"

namespace apptraverse {

class ChatRoom;

// Local-only runtime Presence registry: Aether UID -> PresenceState.
// Shared journal replay must not become the source of Presence.
class ChatPresenceOverlay {
 public:
  void Clear() {
    local_self_ = PresenceState::kUnknown;
    remotes_.clear();
  }

  // Returns true only when the stored value changes.
  bool SetLocalSelf(PresenceState state) {
    if (local_self_ == state) {
      return false;
    }
    local_self_ = state;
    return true;
  }

  // Returns true only when the stored value for uid changes.
  bool SetRemote(std::string uid, PresenceState state) {
    if (uid.empty()) {
      return false;
    }
    auto it = remotes_.find(uid);
    if (it != remotes_.end() && it->second == state) {
      return false;
    }
    remotes_[std::move(uid)] = state;
    return true;
  }

  PresenceState LocalSelf() const { return local_self_; }

  PresenceState Remote(std::string const& uid) const {
    auto it = remotes_.find(uid);
    if (it == remotes_.end()) {
      return PresenceState::kUnknown;
    }
    return it->second;
  }

  // Projects overlay onto ChatClient presentation caches.
  // When local self is not ONLINE, every remote contact is UNKNOWN.
  // Returns the number of ChatClient values that actually changed.
  std::size_t ApplyToRoom(ChatRoom& room,
                          std::string const& local_aether_uid) const;

 private:
  PresenceState local_self_{PresenceState::kUnknown};
  std::unordered_map<std::string, PresenceState> remotes_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_CHAT_PRESENCE_OVERLAY_H_
