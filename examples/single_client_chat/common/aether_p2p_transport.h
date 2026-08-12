#ifndef APPTRAVERSE_EXAMPLES_AETHER_P2P_TRANSPORT_H_
#define APPTRAVERSE_EXAMPLES_AETHER_P2P_TRANSPORT_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "aether/all.h"

#include "aether_p2p_framing.h"
#include "aether_p2p_stream_select.h"

namespace apptraverse::examples {

inline constexpr char kP2pPingPayload[] = "APPTRAVERSE_P2P_PING_V1";
inline constexpr char kP2pPongPayload[] = "APPTRAVERSE_P2P_PONG_V1";

// Opaque framed byte transport over raw Aether P2pStream.
// Reliability (ack / retry / duplicate suppression) belongs to the future
// synchronization layer — this transport does not provide it.
// Peer Aether UID is transport context only and is never placed in the payload.
// Multiple streams may exist for the same remote UID (incoming + outgoing);
// each send picks the most promising one instead of the first one found.
class AetherP2pTransport {
 public:
  using ReceiveHandler = std::function<void(
      ae::Uid const& peer, std::vector<std::uint8_t> const& payload)>;
  using PeerStateHandler =
      std::function<void(ae::Uid const& peer, P2pPeerTransportState state)>;
  using LogHandler = std::function<void(std::string line)>;

  AetherP2pTransport() = default;
  AetherP2pTransport(AetherP2pTransport const&) = delete;
  AetherP2pTransport& operator=(AetherP2pTransport const&) = delete;

  void Start(ae::RcPtr<ae::AetherApp> aether_app, ae::Client::ptr local_client);

  // Creates an outgoing stream only when the peer has none. When streams exist
  // but are only connecting, leaves them alone. When every stream is in link
  // error, Restream()s the best one instead of opening another outgoing.
  void Connect(ae::Uid const& remote_uid);

  // A disposition is not an acknowledgement: kWritable only says the frame was
  // handed to a linked+writable stream.
  P2pSendDisposition Send(ae::Uid const& remote_uid, std::uint8_t const* bytes,
                          std::size_t size);
  P2pSendDisposition Send(ae::Uid const& remote_uid,
                          std::vector<std::uint8_t> const& bytes);
  P2pSendDisposition SendText(ae::Uid const& remote_uid, std::string_view text);

  P2pPeerTransportState PeerState(ae::Uid const& remote_uid) const;
  bool IsPeerWritable(ae::Uid const& remote_uid) const;

  void SetReceiveHandler(ReceiveHandler handler);
  void SetPeerStateHandler(PeerStateHandler handler);
  void SetLogHandler(LogHandler handler);

 private:
  // Runtime only — never serialized.
  struct PeerSession {
    ae::Uid peer{};
    std::shared_ptr<ae::P2pStream> stream;
    ae::Subscription data_sub;
    ae::Subscription stream_update_sub;
    AetherP2pFrameDecoder decoder;
    std::uint64_t creation_order{0};
    std::uint64_t last_receive_order{0};
    P2pStreamDirection direction{P2pStreamDirection::kOutgoing};
    ae::StreamInfo info{};
  };

  struct PeerTracking {
    ae::Uid peer{};
    P2pPeerTransportState last_state{P2pPeerTransportState::kNoStream};
    std::uint64_t last_selected_creation_order{0};
    P2pStreamSelectReason last_select_reason{
        P2pStreamSelectReason::kErrorFallback};
    bool has_selected{false};
  };

  PeerSession* CreateSession(ae::Uid const& peer, ae::P2pPortHandle handle,
                             P2pStreamDirection direction);
  PeerSession* CreateOutgoingSession(ae::Uid const& peer);
  void AttachIncoming(ae::P2pPortHandle handle);

  std::vector<PeerSession*> CollectPeerSessions(ae::Uid const& peer);
  std::vector<P2pSessionSelectCandidate> BuildPeerCandidates(
      std::vector<PeerSession*> const& sessions);
  void RefreshStreamInfo(PeerSession& session);

  void OnStreamUpdate(PeerSession* session);
  void PublishPeerState(ae::Uid const& peer);
  PeerTracking& Tracking(ae::Uid const& peer);

  void OnRawStreamData(PeerSession* session, ae::DataBuffer const& data);
  void EmitPayload(ae::Uid const& peer,
                   std::vector<std::uint8_t> const& payload);
  void Log(std::string line) const;

  ae::RcPtr<ae::AetherApp> aether_app_;
  ae::Client::ptr local_client_;
  ReceiveHandler on_receive_;
  PeerStateHandler on_peer_state_;
  LogHandler on_log_;
  ae::Subscription new_port_sub_;
  std::vector<std::unique_ptr<PeerSession>> sessions_;
  std::vector<PeerTracking> peer_tracking_;
  std::uint64_t next_creation_order_{1};
  std::uint64_t next_receive_order_{1};
};

// Returns true when payload is PING or PONG (answers PONG for PING).
// Returns false for application / sync bytes.
bool TryHandleP2pProbePayload(
    AetherP2pTransport& transport, ae::Uid const& peer,
    std::vector<std::uint8_t> const& payload,
    std::function<void(std::string const&)> const& log_line,
    std::function<void()> const& on_pong_received = {});

// Probe markers only — does not touch Chat / Event / journal.
void AttachPingPongProbe(AetherP2pTransport& transport,
                         std::function<void(std::string const&)> log_line,
                         std::function<void()> on_pong_received = {});

void SendP2pPing(AetherP2pTransport& transport, ae::Uid const& peer,
                 std::function<void(std::string const&)> log_line);

}  // namespace apptraverse::examples

#endif  // APPTRAVERSE_EXAMPLES_AETHER_P2P_TRANSPORT_H_
