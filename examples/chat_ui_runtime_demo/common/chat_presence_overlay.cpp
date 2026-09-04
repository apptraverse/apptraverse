#include "chat_presence_overlay.h"

#include "chat_model.h"

namespace apptraverse {
namespace {

bool ApplyPresenceIfChanged(ChatClient& client, PresenceState state) {
  if (client.GetPresence() == state) {
    return false;
  }
  client.SetPresence(state);
  return true;
}

}  // namespace

std::size_t ChatPresenceOverlay::ApplyToRoom(
    ChatRoom& room, std::string const& local_aether_uid) const {
  std::size_t changed = 0;
  bool const remotes_known = local_self_ == PresenceState::kOnline;
  if (!local_aether_uid.empty()) {
    if (auto self = room.FindClientByAetherUid(local_aether_uid);
        self.is_valid()) {
      if (ApplyPresenceIfChanged(*self, local_self_)) {
        ++changed;
      }
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
    auto const next =
        remotes_known ? Remote(uid) : PresenceState::kUnknown;
    if (ApplyPresenceIfChanged(*client, next)) {
      ++changed;
    }
  }
  return changed;
}

}  // namespace apptraverse
