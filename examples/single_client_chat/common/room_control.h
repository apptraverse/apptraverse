#ifndef APPTRAVERSE_ROOM_CONTROL_H_
#define APPTRAVERSE_ROOM_CONTROL_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "aether/types/uid.h"

namespace apptraverse::chat {

inline constexpr std::uint8_t kRoomControlVersion = 1;
inline constexpr std::size_t kRoomControlMaxNameBytes = 64;
inline constexpr std::size_t kRoomControlMaxParticipants = 64;

enum class RoomControlType : std::uint8_t {
  kClientHello = 1,
  kMembershipPrepare = 2,
  kMembershipPrepared = 3,
  kMembershipSnapshot = 4,
  kMembershipApplied = 5,
  kMembershipActivate = 6,
  kMembershipActivated = 7,
  kMembershipReject = 8,
};

struct RoomParticipantDesc {
  ae::Uid uid{};
  std::uint32_t client_obj_id{0};
  std::string display_name;
};

struct RoomControlMessage {
  RoomControlType type{RoomControlType::kClientHello};
  std::uint64_t revision{0};
  std::uint32_t client_obj_id{0};  // ClientHello / reject context
  std::string display_name;        // ClientHello / reject reason
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
