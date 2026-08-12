#include "chat_presence.h"

#include <cstring>
#include <string_view>

namespace apptraverse::examples {
namespace {

constexpr std::string_view kOnlinePayload = "APPTRAVERSE_CHAT_ONLINE_V1";
constexpr std::string_view kHeartbeatPayload = "APPTRAVERSE_CHAT_HEARTBEAT_V1";
constexpr std::string_view kOfflinePayload = "APPTRAVERSE_CHAT_OFFLINE_V1";

std::vector<std::uint8_t> EncodePayload(std::string_view payload) {
  return std::vector<std::uint8_t>(payload.begin(), payload.end());
}

bool EqualsPayload(std::uint8_t const* data, std::size_t size,
                   std::string_view expected) {
  if (data == nullptr || size != expected.size()) {
    return false;
  }
  return std::memcmp(data, expected.data(), size) == 0;
}

}  // namespace

std::vector<std::uint8_t> EncodeChatPresence(ChatPresenceMessage message) {
  switch (message) {
    case ChatPresenceMessage::kOnline:
      return EncodePayload(kOnlinePayload);
    case ChatPresenceMessage::kHeartbeat:
      return EncodePayload(kHeartbeatPayload);
    case ChatPresenceMessage::kOffline:
      return EncodePayload(kOfflinePayload);
  }
  return {};
}

std::optional<ChatPresenceMessage> TryDecodeChatPresence(
    std::uint8_t const* data, std::size_t size) {
  if (EqualsPayload(data, size, kOnlinePayload)) {
    return ChatPresenceMessage::kOnline;
  }
  if (EqualsPayload(data, size, kHeartbeatPayload)) {
    return ChatPresenceMessage::kHeartbeat;
  }
  if (EqualsPayload(data, size, kOfflinePayload)) {
    return ChatPresenceMessage::kOffline;
  }
  return std::nullopt;
}

}  // namespace apptraverse::examples
