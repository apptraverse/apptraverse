#include "aether_stream_frame.h"

#include <cstring>

namespace apptraverse::example::chat_demo {

std::vector<std::uint8_t> EncodeAetherFrame(
    AetherFrameKind kind,
    std::span<std::uint8_t const> payload) {
  std::vector<std::uint8_t> bytes(kAetherStreamHeaderSize + payload.size());
  bytes[0] = 'A';
  bytes[1] = 'T';
  bytes[2] = 'R';
  bytes[3] = 'N';
  bytes[4] = 1;  // version 1
  bytes[5] = static_cast<std::uint8_t>(kind);
  auto const size = static_cast<std::uint32_t>(payload.size());
  bytes[6] = static_cast<std::uint8_t>((size >> 0) & 0xFF);
  bytes[7] = static_cast<std::uint8_t>((size >> 8) & 0xFF);
  bytes[8] = static_cast<std::uint8_t>((size >> 16) & 0xFF);
  bytes[9] = static_cast<std::uint8_t>((size >> 24) & 0xFF);
  if (!payload.empty()) {
    std::memcpy(bytes.data() + kAetherStreamHeaderSize, payload.data(), payload.size());
  }
  return bytes;
}

bool DecodeAetherFrame(
    std::vector<std::uint8_t> const& bytes,
    AetherFrameKind& out_kind,
    std::vector<std::uint8_t>& out_payload) {
  if (bytes.size() < kAetherStreamHeaderSize) {
    return false;
  }
  if (bytes[0] != 'A' || bytes[1] != 'T' || bytes[2] != 'R' || bytes[3] != 'N') {
    return false;
  }
  if (bytes[4] != 1) {
    return false;
  }
  auto const raw_kind = bytes[5];
  if (raw_kind != static_cast<std::uint8_t>(AetherFrameKind::kApplication) &&
      raw_kind != static_cast<std::uint8_t>(AetherFrameKind::kHeartbeatPing) &&
      raw_kind != static_cast<std::uint8_t>(AetherFrameKind::kHeartbeatPong)) {
    return false;
  }

  std::uint32_t const payload_size = static_cast<std::uint32_t>(bytes[6]) |
                                     (static_cast<std::uint32_t>(bytes[7]) << 8) |
                                     (static_cast<std::uint32_t>(bytes[8]) << 16) |
                                     (static_cast<std::uint32_t>(bytes[9]) << 24);

  // Require bytes.size() == 10 + payload_size (no trailing bytes, full payload present)
  if (bytes.size() != kAetherStreamHeaderSize + payload_size) {
    return false;
  }

  if (raw_kind == static_cast<std::uint8_t>(AetherFrameKind::kApplication)) {
    if (payload_size > kMaxApplicationPayloadSize) {
      return false;
    }
  } else {
    // kHeartbeatPing and kHeartbeatPong must be exactly 8 bytes
    if (payload_size != kHeartbeatPayloadSize) {
      return false;
    }
  }

  out_kind = static_cast<AetherFrameKind>(raw_kind);
  out_payload.assign(bytes.begin() + kAetherStreamHeaderSize, bytes.end());
  return true;
}

std::vector<std::uint8_t> EncodeHeartbeatNonce(std::uint64_t nonce) {
  std::vector<std::uint8_t> payload(kHeartbeatPayloadSize);
  for (int i = 0; i < 8; ++i) {
    payload[i] = static_cast<std::uint8_t>((nonce >> (i * 8)) & 0xFF);
  }
  return payload;
}

std::uint64_t DecodeHeartbeatNonce(std::span<std::uint8_t const> payload) {
  if (payload.size() < kHeartbeatPayloadSize) {
    return 0;
  }
  std::uint64_t nonce = 0;
  for (int i = 0; i < 8; ++i) {
    nonce |= (static_cast<std::uint64_t>(payload[i]) << (i * 8));
  }
  return nonce;
}

}  // namespace apptraverse::example::chat_demo
