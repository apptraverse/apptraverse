#ifndef APPTRAVERSE_EXAMPLE_CHAT_DEMO_AETHER_FRAME_ENDPOINT_H_
#define APPTRAVERSE_EXAMPLE_CHAT_DEMO_AETHER_FRAME_ENDPOINT_H_

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "chat_presence.h"

namespace apptraverse::example::chat_demo {

// Network endpoint lifecycle used by ChatSession. Concrete ChatAetherRuntime
// owns the real Aether client; tests may supply a fake that routes opaque
// bytes and scripted readiness/presence without a Domain or chat model.
class IAetherFrameEndpoint {
 public:
  using LocalUidCallback = std::function<void(std::string uid)>;
  using ReadyCallback = std::function<void()>;
  using FailedCallback = std::function<void(std::string error)>;
  using FrameCallback =
      std::function<void(std::string source_uid,
                         std::vector<std::uint8_t> bytes)>;
  using PresenceCallback =
      std::function<void(std::string peer_uid, PeerPresence presence)>;
  using LocalConnectivityCallback =
      std::function<void(bool has_schedule, bool any_online)>;

  struct Config {
    std::filesystem::path state_dir;
    std::string client_name;
    std::uint64_t heartbeat_period_ms{2000};
    std::uint64_t offline_after_ms{7000};
  };

  virtual ~IAetherFrameEndpoint() = default;

  virtual void Start(Config config, LocalUidCallback on_uid,
                     ReadyCallback on_ready, FailedCallback on_failed,
                     FrameCallback on_frame, PresenceCallback on_presence,
                     LocalConnectivityCallback on_local_connectivity = {}) = 0;

  virtual void OpenPeer(std::string peer_uid) = 0;
  virtual void ClosePeer(std::string peer_uid) = 0;

  virtual void Send(std::string peer_uid,
                    std::vector<std::uint8_t> bytes) = 0;
  virtual void SetFrameCallback(FrameCallback callback) = 0;

  virtual void RequestStop() = 0;
  virtual void Join() = 0;
};

}  // namespace apptraverse::example::chat_demo

#endif  // APPTRAVERSE_EXAMPLE_CHAT_DEMO_AETHER_FRAME_ENDPOINT_H_
