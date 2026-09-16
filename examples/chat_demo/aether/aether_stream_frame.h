#ifndef APPTRAVERSE_EXAMPLE_CHAT_DEMO_AETHER_STREAM_FRAME_H_
#define APPTRAVERSE_EXAMPLE_CHAT_DEMO_AETHER_STREAM_FRAME_H_

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace apptraverse::example::chat_demo {

enum class AetherFrameKind : std::uint8_t {
  kApplication = 1,
  kHeartbeatPing = 2,
  kHeartbeatPong = 3,
  kControl = 4,
};

inline constexpr std::size_t kAetherStreamHeaderSize = 10;
inline constexpr std::size_t kMaxApplicationPayloadSize = 16 * 1024 * 1024;  // 16 MiB
inline constexpr std::size_t kHeartbeatPayloadSize = 8;
inline constexpr std::size_t kMaxControlPayloadSize = 1024;

std::vector<std::uint8_t> EncodeAetherFrame(
    AetherFrameKind kind,
    std::span<std::uint8_t const> payload);

bool DecodeAetherFrame(
    std::vector<std::uint8_t> const& bytes,
    AetherFrameKind& out_kind,
    std::vector<std::uint8_t>& out_payload);

std::vector<std::uint8_t> EncodeHeartbeatNonce(std::uint64_t nonce);
std::uint64_t DecodeHeartbeatNonce(std::span<std::uint8_t const> payload);

}  // namespace apptraverse::example::chat_demo

#endif  // APPTRAVERSE_EXAMPLE_CHAT_DEMO_AETHER_STREAM_FRAME_H_
