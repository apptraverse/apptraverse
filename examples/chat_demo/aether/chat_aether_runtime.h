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
#include "aether/client_messages/p2p_safe_message_stream.h"
#include "aether/types/uid.h"

#include "aether_frame_endpoint.h"
#include "aether_stream_frame.h"
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
  using ControlCallback = IAetherFrameEndpoint::ControlCallback;
  using Config = IAetherFrameEndpoint::Config;

  ChatAetherRuntime();
  ~ChatAetherRuntime() override;

  ChatAetherRuntime(ChatAetherRuntime const&) = delete;
  ChatAetherRuntime& operator=(ChatAetherRuntime const&) = delete;

  void Start(Config config, LocalUidCallback on_uid, ReadyCallback on_ready,
             FailedCallback on_failed, FrameCallback on_frame,
             PresenceCallback on_presence,
             LocalConnectivityCallback on_local_connectivity = {},
             ControlCallback on_control = {}) override;

  void OpenPeer(std::string peer_uid) override;
  void Send(std::string peer_uid, std::vector<std::uint8_t> bytes) override;
  void SendControl(std::string peer_uid,
                   std::vector<std::uint8_t> bytes) override;
  void ClosePeer(std::string peer_uid) override;

  void RequestStop() override;
  void Join() override;

  void SetFrameCallback(FrameCallback on_frame) override;

 private:
  enum class CommandType : std::uint8_t {
    kOpenPeer = 1,
    kSend = 2,
    kClosePeer = 3,
    kSendControl = 4,
  };

  struct Command {
    CommandType type{CommandType::kOpenPeer};
    std::string peer_uid;
    std::vector<std::uint8_t> bytes;
  };

  struct PendingOut {
    AetherFrameKind kind{AetherFrameKind::kApplication};
    std::vector<std::uint8_t> bytes;
  };

  struct PeerState {
    std::string uid_text;
    ae::Uid uid;

    // P2pSafeStream owns the underlying P2pStream and fragments to channel MTU.
    std::shared_ptr<ae::P2pStream> raw_p2p;
    std::unique_ptr<ae::P2pSafeStream> stream;

    ae::Subscription data_sub;
    ae::Subscription update_sub;

    std::vector<ae::Subscription> write_subs;

    std::deque<PendingOut> pending_out;

    // P2pSafeStream acknowledges one Write at a time end-to-end. Stacking
    // SharedSyncRuntime retries and heartbeats into concurrent Writes stalls
    // the send window so Host→Client Events never complete after NodeState.
    bool write_in_flight{false};
    AetherFrameKind in_flight_kind{AetherFrameKind::kApplication};
    std::vector<std::uint8_t> in_flight_bytes;
    std::uint64_t write_started_ms{0};

    // After the Client's initial ACK Write succeeds, defer larger application
    // Sends until one post-ACK application frame is received. Simultaneous
    // Host↔Client Event Writes on the same SafeStream pair hang without
    // WRITE_OK; Host (which receives the ACK) may send first.
    // Arm only once per peer (re-arming on Event ACKs deadlocks both sides).
    bool defer_large_app_until_rx{false};
    bool join_half_duplex_used{false};
    bool post_join_safestream_reset{false};
    bool pending_join_safestream_reset{false};
    std::uint64_t join_reset_ready_ms{0};

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
                  LocalConnectivityCallback on_local_connectivity,
                  ControlCallback on_control);

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
  ControlCallback on_control_;
};

}  // namespace apptraverse::example::chat_demo

#endif  // APPTRAVERSE_EXAMPLE_CHAT_DEMO_CHAT_AETHER_RUNTIME_H_
