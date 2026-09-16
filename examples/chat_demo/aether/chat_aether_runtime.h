#ifndef APPTRAVERSE_EXAMPLE_CHAT_DEMO_CHAT_AETHER_RUNTIME_H_
#define APPTRAVERSE_EXAMPLE_CHAT_DEMO_CHAT_AETHER_RUNTIME_H_

#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "aether/aether_app.h"
#include "aether/client.h"
#include "aether/client_messages/p2p_message_stream.h"
#include "aether/client_messages/p2p_port_handle.h"
#include "aether/types/uid.h"

#include "aether_frame_endpoint.h"
#include "chat_presence.h"

namespace apptraverse::example::chat_demo {

class ChatAetherRuntime : public IAetherFrameEndpoint {
 public:
  using LocalUidCallback = IAetherFrameEndpoint::LocalUidCallback;
  using ReadyCallback = IAetherFrameEndpoint::ReadyCallback;
  using FailedCallback = IAetherFrameEndpoint::FailedCallback;
  using FrameCallback = IAetherFrameEndpoint::FrameCallback;
  using PresenceCallback = IAetherFrameEndpoint::PresenceCallback;
  using LocalConnectivityCallback = IAetherFrameEndpoint::LocalConnectivityCallback;
  using Config = IAetherFrameEndpoint::Config;

  ChatAetherRuntime();
  ~ChatAetherRuntime() override;

  ChatAetherRuntime(ChatAetherRuntime const&) = delete;
  ChatAetherRuntime& operator=(ChatAetherRuntime const&) = delete;

  void Start(Config config, LocalUidCallback on_uid, ReadyCallback on_ready,
             FailedCallback on_failed, FrameCallback on_frame,
             PresenceCallback on_presence,
             LocalConnectivityCallback on_local_connectivity = {}) override;

  void OpenPeer(std::string peer_uid) override;
  void Send(std::string peer_uid, std::vector<std::uint8_t> bytes) override;
  void ClosePeer(std::string peer_uid) override;

  void RequestStop() override;
  void Join() override;

  void SetFrameCallback(FrameCallback on_frame) override;

 private:
  enum class CommandType : std::uint8_t {
    kOpenPeer = 1,
    kSend = 2,
    kClosePeer = 3,
  };

  struct Command {
    CommandType type{CommandType::kOpenPeer};
    std::string peer_uid;
    std::vector<std::uint8_t> bytes;
  };

  struct PeerState {
    std::string uid_text;
    ae::Uid uid;

    std::shared_ptr<ae::P2pStream> stream;

    ae::Subscription data_sub;
    ae::Subscription update_sub;

    std::vector<ae::Subscription> write_subs;

    std::deque<std::vector<std::uint8_t>> pending_out;

    bool stream_linked{false};

    std::uint64_t last_rx_ms{0};
    std::uint64_t last_heartbeat_tx_ms{0};
    std::uint64_t last_heartbeat_rx_ms{0};

    PeerPresence reported_presence{PeerPresence::kUnknown};
  };

  void Enqueue(Command command);
  void ThreadMain(Config config, LocalUidCallback on_uid,
                  ReadyCallback on_ready, FailedCallback on_failed,
                  FrameCallback on_frame, PresenceCallback on_presence,
                  LocalConnectivityCallback on_local_connectivity);

  std::atomic<bool> stop_{false};
  std::thread thread_;

  std::mutex command_mu_;
  std::queue<Command> commands_;

  std::mutex callback_mu_;
  LocalUidCallback on_uid_;
  ReadyCallback on_ready_;
  FailedCallback on_failed_;
  FrameCallback on_frame_;
  PresenceCallback on_presence_;
  LocalConnectivityCallback on_local_connectivity_;
};

}  // namespace apptraverse::example::chat_demo

#endif  // APPTRAVERSE_EXAMPLE_CHAT_DEMO_CHAT_AETHER_RUNTIME_H_
