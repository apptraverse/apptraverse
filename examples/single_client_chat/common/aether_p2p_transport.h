#ifndef APPTRAVERSE_EXAMPLES_AETHER_P2P_TRANSPORT_H_
#define APPTRAVERSE_EXAMPLES_AETHER_P2P_TRANSPORT_H_

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "aether/all.h"

#include "aether_p2p_framing.h"

namespace apptraverse::examples {

inline constexpr char kP2pPingPayload[] = "APPTRAVERSE_P2P_PING_V1";
inline constexpr char kP2pPongPayload[] = "APPTRAVERSE_P2P_PONG_V1";

inline constexpr ae::SafeStreamConfig kAetherP2pSafeStreamConfig{
    .window_size = AE_SAFE_STREAM_CAPACITY / 2 - 1,
    .max_packet_size = AE_SAFE_STREAM_CAPACITY / 2 - 1,
    .max_repeat_count = 10,
    .wait_ack_timeout = std::chrono::seconds{5},
    .send_ack_timeout = std::chrono::seconds{0},
    .send_repeat_timeout = std::chrono::seconds{2},
};

// Opaque byte transport over Aether P2pSafeStream. No AppTraverse model types.
class AetherP2pTransport {
 public:
  using ReceiveHandler = std::function<void(
      ae::Uid const& peer, std::vector<std::uint8_t> const& payload)>;

  AetherP2pTransport() = default;
  AetherP2pTransport(AetherP2pTransport const&) = delete;
  AetherP2pTransport& operator=(AetherP2pTransport const&) = delete;

  void Start(ae::RcPtr<ae::AetherApp> aether_app, ae::Client::ptr local_client);
  void Connect(ae::Uid const& remote_uid);
  void Send(ae::Uid const& remote_uid, std::uint8_t const* bytes,
            std::size_t size);
  void Send(ae::Uid const& remote_uid, std::vector<std::uint8_t> const& bytes);
  void SendText(ae::Uid const& remote_uid, std::string_view text);

  void SetReceiveHandler(ReceiveHandler handler);

 private:
  struct PeerSession {
    ae::Uid peer{};
    std::shared_ptr<ae::P2pStream> p2p_stream;
    std::unique_ptr<ae::P2pSafeStream> safe_stream;
    ae::Subscription data_sub;
    AetherP2pFrameDecoder decoder;
  };

  PeerSession* FindSession(ae::Uid const& remote_uid);
  PeerSession* EnsureOutgoingSession(ae::Uid const& remote_uid);
  void AttachIncoming(ae::P2pPortHandle handle);
  void OnRawStreamData(PeerSession* session, ae::DataBuffer const& data);
  void EmitPayload(ae::Uid const& peer,
                   std::vector<std::uint8_t> const& payload);

  ae::RcPtr<ae::AetherApp> aether_app_;
  ae::Client::ptr local_client_;
  ReceiveHandler on_receive_;
  ae::Subscription new_port_sub_;
  std::map<ae::Uid, std::unique_ptr<PeerSession>> sessions_;
};

// Probe markers only — does not touch Chat / Event / journal.
void AttachPingPongProbe(AetherP2pTransport& transport,
                         std::function<void(std::string const&)> log_line,
                         std::function<void()> on_pong_received = {});

void SendP2pPing(AetherP2pTransport& transport, ae::Uid const& peer,
                 std::function<void(std::string const&)> log_line);

}  // namespace apptraverse::examples

#endif  // APPTRAVERSE_EXAMPLES_AETHER_P2P_TRANSPORT_H_
