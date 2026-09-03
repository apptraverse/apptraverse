#ifndef APPTRAVERSE_CHAT_PRESENCE_OVERLAY_H_
#define APPTRAVERSE_CHAT_PRESENCE_OVERLAY_H_

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

  void SetLocalSelf(PresenceState state) { local_self_ = state; }

  void SetRemote(std::string uid, PresenceState state) {
    if (uid.empty()) {
      return;
    }
    remotes_[std::move(uid)] = state;
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
  void ApplyToRoom(ChatRoom& room, std::string const& local_aether_uid) const;

 private:
  PresenceState local_self_{PresenceState::kUnknown};
  std::unordered_map<std::string, PresenceState> remotes_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_CHAT_PRESENCE_OVERLAY_H_
