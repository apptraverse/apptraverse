#ifndef APPTRAVERSE_ROOM_INBOUND_DEMUX_H_
#define APPTRAVERSE_ROOM_INBOUND_DEMUX_H_

#include <cstdint>
#include <optional>
#include <vector>

#include "room_control.h"

namespace apptraverse::chat {

enum class RoomInboundKind : std::uint8_t {
  kNotRoomControl = 0,
  kRoomControlOk = 1,
  kRoomControlDecodeFail = 2,
};

// ATRM-priority demux step: if payload has room-control magic, never treat it
// as Chat sync / presence. DecodeFail must be dropped by the caller.
inline RoomInboundKind ClassifyRoomControlInbound(
    std::uint8_t const* data, std::size_t size,
    std::optional<RoomControlMessage>* out_msg) {
  if (out_msg != nullptr) {
    out_msg->reset();
  }
  if (!IsRoomControlPayload(data, size)) {
    return RoomInboundKind::kNotRoomControl;
  }
  auto decoded = TryDecodeRoomControl(data, size);
  if (!decoded.has_value()) {
    return RoomInboundKind::kRoomControlDecodeFail;
  }
  if (out_msg != nullptr) {
    *out_msg = std::move(*decoded);
  }
  return RoomInboundKind::kRoomControlOk;
}

inline RoomInboundKind ClassifyRoomControlInbound(
    std::vector<std::uint8_t> const& bytes,
    std::optional<RoomControlMessage>* out_msg) {
  return ClassifyRoomControlInbound(bytes.data(), bytes.size(), out_msg);
}

}  // namespace apptraverse::chat

#endif  // APPTRAVERSE_ROOM_INBOUND_DEMUX_H_
