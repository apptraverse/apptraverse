#include "aether_p2p_framing.h"

#include <cstring>

namespace apptraverse::examples {
namespace {

constexpr std::size_t kHeaderSize = 4;

std::uint32_t ReadU32Le(std::uint8_t const* bytes) {
  return static_cast<std::uint32_t>(bytes[0]) |
         (static_cast<std::uint32_t>(bytes[1]) << 8) |
         (static_cast<std::uint32_t>(bytes[2]) << 16) |
         (static_cast<std::uint32_t>(bytes[3]) << 24);
}

void WriteU32Le(std::uint8_t* bytes, std::uint32_t value) {
  bytes[0] = static_cast<std::uint8_t>(value);
  bytes[1] = static_cast<std::uint8_t>(value >> 8);
  bytes[2] = static_cast<std::uint8_t>(value >> 16);
  bytes[3] = static_cast<std::uint8_t>(value >> 24);
}

}  // namespace

void AetherP2pFrameDecoder::Append(std::uint8_t const* data, std::size_t size) {
  if (data == nullptr || size == 0) {
    return;
  }
  buffer_.insert(buffer_.end(), data, data + size);
}

void AetherP2pFrameDecoder::Drain(FrameHandler const& on_frame) {
  while (buffer_.size() >= kHeaderSize) {
    auto const payload_size =
        static_cast<std::size_t>(ReadU32Le(buffer_.data()));
    auto const frame_size = kHeaderSize + payload_size;
    if (buffer_.size() < frame_size) {
      return;
    }
    std::vector<std::uint8_t> payload(buffer_.begin() + kHeaderSize,
                                      buffer_.begin() + frame_size);
    buffer_.erase(buffer_.begin(),
                  buffer_.begin() + static_cast<std::ptrdiff_t>(frame_size));
    if (on_frame) {
      on_frame(payload);
    }
  }
}

std::vector<std::uint8_t> EncodeAetherP2pFrame(std::uint8_t const* payload,
                                               std::size_t payload_size) {
  std::vector<std::uint8_t> frame(kHeaderSize + payload_size);
  WriteU32Le(frame.data(), static_cast<std::uint32_t>(payload_size));
  if (payload_size != 0 && payload != nullptr) {
    std::memcpy(frame.data() + kHeaderSize, payload, payload_size);
  }
  return frame;
}

}  // namespace apptraverse::examples
