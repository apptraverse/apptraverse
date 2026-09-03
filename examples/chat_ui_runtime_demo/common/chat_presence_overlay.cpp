#include "chat_presence_overlay.h"

#include "chat_model.h"

namespace apptraverse {

void ChatPresenceOverlay::ApplyToRoom(ChatRoom& room,
                                      std::string const& local_aether_uid) const {
  bool const remotes_known = local_self_ == PresenceState::kOnline;
  if (!local_aether_uid.empty()) {
    if (auto self = room.FindClientByAetherUid(local_aether_uid);
        self.is_valid()) {
      self->SetPresence(local_self_);
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
    if (!remotes_known) {
      client->SetPresence(PresenceState::kUnknown);
      continue;
    }
    client->SetPresence(Remote(uid));
  }
}

}  // namespace apptraverse
