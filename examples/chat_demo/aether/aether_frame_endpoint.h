#ifndef APPTRAVERSE_EXAMPLE_CHAT_DEMO_AETHER_FRAME_ENDPOINT_H_
#define APPTRAVERSE_EXAMPLE_CHAT_DEMO_AETHER_FRAME_ENDPOINT_H_

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace apptraverse::example::chat_demo {

class IAetherFrameEndpoint {
 public:
  using FrameCallback =
      std::function<void(
          std::string source_uid,
          std::vector<std::uint8_t> bytes)>;

  virtual ~IAetherFrameEndpoint() = default;

  virtual void Send(
      std::string peer_uid,
      std::vector<std::uint8_t> bytes) = 0;

  virtual void SetFrameCallback(FrameCallback callback) = 0;
};

}  // namespace apptraverse::example::chat_demo

#endif  // APPTRAVERSE_EXAMPLE_CHAT_DEMO_AETHER_FRAME_ENDPOINT_H_
