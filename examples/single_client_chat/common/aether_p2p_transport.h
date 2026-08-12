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
#include "aether_p2p_outgoing_routing.h"

namespace apptraverse::examples {

inline constexpr char kP2pPingPayload[] = "APPTRAVERSE_P2P_PING_V1";
inline constexpr char kP2pPongPayload[] = "APPTRAVERSE_P2P_PONG_V1";

// Opaque framed byte transport over raw Aether P2pStream.
// Reliability (ack / retry / duplicate suppression) belongs to the
// synchronization layer — this transport does not provide it.
// Peer Aether UID is transport context only and is never placed in the payload.
//
// Directional model per remote UID:
// - exactly one active outgoing P2pStream is used for all Send;
// - any number of incoming (and retired outgoing) streams receive only;
// - receive activity never influences outgoing selection.
class AetherP2pTransport {
 public:
  using ReceiveHandler = std::function<void(
      ae::Uid const& peer, std::vector<std::uint8_t> const& payload)>;
  using OutgoingStateHandler =
      std::function<void(ae::Uid const& peer, P2pOutgoingState state)>;
  using LogHandler = std::function<void(std::string line)>;

  AetherP2pTransport() = default;
  AetherP2pTransport(AetherP2pTransport const&) = delete;
  AetherP2pTransport& operator=(AetherP2pTransport const&) = delete;

  void Start(ae::RcPtr<ae::AetherApp> aether_app, ae::Client::ptr local_client);

  // Always ensures an active outgoing stream to peer. Presence of an incoming
  // stream is never a reason to skip creating the outgoing path.
  void Connect(ae::Uid const& remote_uid);
  void EnsureOutgoing(ae::Uid const& peer);

  void RestreamOutgoing(ae::Uid const& peer);
  void ReplaceOutgoing(ae::Uid const& peer);

  // A disposition is not an acknowledgement: kWritable only says the frame was
  // handed to a linked+writable active outgoing stream.
  P2pSendResult Send(ae::Uid const& remote_uid, std::uint8_t const* bytes,
                     std::size_t size);
  P2pSendResult Send(ae::Uid const& remote_uid,
                     std::vector<std::uint8_t> const& bytes);
  P2pSendResult SendText(ae::Uid const& remote_uid, std::string_view text);

  P2pOutgoingState OutgoingState(ae::Uid const& remote_uid) const;
  bool IsOutgoingWritable(ae::Uid const& remote_uid) const;

  void SetReceiveHandler(ReceiveHandler handler);
  void SetOutgoingStateHandler(OutgoingStateHandler handler);
  void SetLogHandler(LogHandler handler);

 private:
  // Runtime only — never serialized.
  struct PeerSession {
    ae::Uid peer{};
    std::shared_ptr<ae::P2pStream> stream;
    ae::Subscription data_sub;
    ae::Subscription stream_update_sub;
    AetherP2pFrameDecoder decoder;
    P2pStreamDirection direction{P2pStreamDirection::kOutgoing};
    std::uint64_t generation{0};
    bool active_for_send{false};
    ae::StreamInfo info{};
  };

  struct PeerTransport {
    ae::Uid remote_uid{};
    std::uint64_t active_outgoing_generation{0};
    std::vector<std::unique_ptr<PeerSession>> sessions;
    P2pOutgoingState last_published_outgoing_state{P2pOutgoingState::kMissing};
  };

  PeerTransport& EnsurePeerTransport(ae::Uid const& peer);
  PeerTransport* FindPeerTransport(ae::Uid const& peer);
  PeerTransport const* FindPeerTransport(ae::Uid const& peer) const;
  PeerSession* FindActiveOutgoing(PeerTransport& peer);
  PeerSession const* FindActiveOutgoing(PeerTransport const& peer) const;

  PeerSession* CreateOutgoingSession(PeerTransport& peer);
  PeerSession* CreateIncomingSession(PeerTransport& peer,
                                     ae::P2pPortHandle handle);
  void AttachIncoming(ae::P2pPortHandle handle);

  void RefreshStreamInfo(PeerSession& session);
  P2pOutgoingState ComputeState(PeerSession const* active) const;

  void OnStreamUpdate(PeerSession* session);
  void PublishOutgoingState(PeerTransport& peer);

  void OnRawStreamData(PeerSession* session, ae::DataBuffer const& data);
  void EmitPayload(ae::Uid const& peer,
                   std::vector<std::uint8_t> const& payload);
  void Log(std::string line) const;

  ae::RcPtr<ae::AetherApp> aether_app_;
  ae::Client::ptr local_client_;
  ReceiveHandler on_receive_;
  OutgoingStateHandler on_outgoing_state_;
  LogHandler on_log_;
  ae::Subscription new_port_sub_;
  std::vector<std::unique_ptr<PeerTransport>> peers_;
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
