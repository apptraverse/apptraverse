#ifndef APPTRAVERSE_CHAT_PRESENCE_OVERLAY_H_
#define APPTRAVERSE_CHAT_PRESENCE_OVERLAY_H_

#include <optional>
#include <string>
#include <unordered_map>

namespace apptraverse {

class ChatRoom;

// Local-only presence overlay. Shared journal replay must not reset these values.
class ChatPresenceOverlay {
 public:
  void SetLocalSelfOnline(bool online) { local_self_online_ = online; }

  void SetRemoteOnline(std::string uid, bool online) {
    if (uid.empty()) {
      return;
    }
    remote_online_[std::move(uid)] = online;
  }

  std::optional<bool> LocalSelfOnline() const { return local_self_online_; }

  std::optional<bool> RemoteOnline(std::string const& uid) const {
    auto it = remote_online_.find(uid);
    if (it == remote_online_.end()) {
      return std::nullopt;
    }
    return it->second;
  }

  void ApplyToRoom(ChatRoom& room, std::string const& local_aether_uid) const;

 private:
  std::optional<bool> local_self_online_;
  std::unordered_map<std::string, bool> remote_online_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_CHAT_PRESENCE_OVERLAY_H_
