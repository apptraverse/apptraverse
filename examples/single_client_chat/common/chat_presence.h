#ifndef APPTRAVERSE_CHAT_PRESENCE_H_
#define APPTRAVERSE_CHAT_PRESENCE_H_

#include <cstdint>
#include <optional>
#include <vector>

namespace apptraverse::chat {

enum class ChatPresenceMessage { kOnline, kHeartbeat, kOffline };

std::vector<std::uint8_t> EncodeChatPresence(ChatPresenceMessage message);

std::optional<ChatPresenceMessage> TryDecodeChatPresence(
    std::uint8_t const* data, std::size_t size);

inline std::optional<ChatPresenceMessage> TryDecodeChatPresence(
    std::vector<std::uint8_t> const& bytes) {
  return TryDecodeChatPresence(bytes.data(), bytes.size());
}

}  // namespace apptraverse::chat

#endif  // APPTRAVERSE_CHAT_PRESENCE_H_