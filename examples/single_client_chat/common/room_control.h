#ifndef APPTRAVERSE_ROOM_CONTROL_H_
#define APPTRAVERSE_ROOM_CONTROL_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "aether/types/uid.h"

namespace apptraverse::chat {

inline constexpr std::uint8_t kRoomControlVersion = 2;
inline constexpr std::size_t kRoomControlMaxNameBytes = 64;
inline constexpr std::size_t kRoomControlMaxParticipants = 64;

enum class RoomControlType : std::uint8_t {
  kJoinRoomRequest = 1,
  kJoinRoomAccepted = 2,
  kJoinRoomRejected = 3,
  kParticipantsChanged = 4,
  kJoinRoomAcceptedAck = 5,  // optional; must not block UI/AddPeer/sync
};

struct RoomParticipantDesc {
  ae::Uid uid{};
  std::uint32_t client_obj_id{0};
  std::string display_name;
};

struct RoomControlMessage {
  RoomControlType type{RoomControlType::kJoinRoomRequest};
  std::uint64_t revision{0};
  std::uint64_t request_id{0};  // stable client-generated
  std::uint32_t client_obj_id{0};
  std::string display_name;
  std::vector<RoomParticipantDesc> participants;
};

bool IsRoomControlPayload(std::uint8_t const* data, std::size_t size);

std::vector<std::uint8_t> EncodeRoomControl(RoomControlMessage const& msg);

std::optional<RoomControlMessage> TryDecodeRoomControl(
    std::uint8_t const* data, std::size_t size);

inline std::optional<RoomControlMessage> TryDecodeRoomControl(
    std::vector<std::uint8_t> const& bytes) {
  return TryDecodeRoomControl(bytes.data(), bytes.size());
}

}  // namespace apptraverse::chat

#endif  // APPTRAVERSE_ROOM_CONTROL_H_
