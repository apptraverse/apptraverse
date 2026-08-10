#ifndef APPTRAVERSE_EXAMPLES_AETHER_P2P_FRAMING_H_
#define APPTRAVERSE_EXAMPLES_AETHER_P2P_FRAMING_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace apptraverse::examples {

// Little-endian uint32 payload_size + payload bytes.
// Fixed byte order shared by Windows and Android.
class AetherP2pFrameDecoder {
 public:
  using FrameHandler =
      std::function<void(std::vector<std::uint8_t> const& payload)>;

  void Append(std::uint8_t const* data, std::size_t size);
  void Drain(FrameHandler const& on_frame);

  std::vector<std::uint8_t> const& buffer() const { return buffer_; }

 private:
  std::vector<std::uint8_t> buffer_;
};

std::vector<std::uint8_t> EncodeAetherP2pFrame(
    std::uint8_t const* payload, std::size_t payload_size);

inline std::vector<std::uint8_t> EncodeAetherP2pFrame(
    std::vector<std::uint8_t> const& payload) {
  return EncodeAetherP2pFrame(payload.data(), payload.size());
}

}  // namespace apptraverse::examples

#endif  // APPTRAVERSE_EXAMPLES_AETHER_P2P_FRAMING_H_
