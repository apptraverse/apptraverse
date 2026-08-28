#include "chat_presence_overlay.h"

#include "chat_model.h"

namespace apptraverse {

void ChatPresenceOverlay::ApplyToRoom(ChatRoom& room,
                                      std::string const& local_aether_uid) const {
  if (local_self_online_.has_value()) {
    if (auto self = room.FindClientByAetherUid(local_aether_uid); self.is_valid()) {
      self->SetOnline(*local_self_online_);
    }
  }
  for (auto const& client : room.clients) {
    if (!client.is_valid()) {
      continue;
    }
    auto const uid = client->AetherUidText();
    if (uid.empty() || uid == local_aether_uid) {
      continue;
    }
    if (auto online = RemoteOnline(uid)) {
      client->SetOnline(*online);
    }
  }
}

}  // namespace apptraverse
