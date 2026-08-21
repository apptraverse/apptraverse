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

namespace apptraverse::examples {

inline constexpr char kP2pPingPayload[] = "APPTRAVERSE_P2P_PING_V1";
inline constexpr char kP2pPongPayload[] = "APPTRAVERSE_P2P_PONG_V1";

// Opaque framed byte transport over raw Aether P2pStream.
// Reliability (ack / retry / duplicate suppression) belongs to the
// synchronization layer — this transport does not provide it.
// Peer Aether UID is transport context only and is never placed in the payload.
//
// One PeerSession (one P2pStream) per remote UID in a process.
class AetherP2pTransport {
 public:
  using ReceiveHandler = std::function<void(
      ae::Uid const& peer, std::vector<std::uint8_t> const& payload)>;
  using LogHandler = std::function<void(std::string line)>;
  // Invoked on the calling thread immediately before stream->Write.
  using PreWriteHandler = std::function<void(
      ae::Uid const& peer, std::size_t frame_bytes)>;

  AetherP2pTransport() = default;
  AetherP2pTransport(AetherP2pTransport const&) = delete;
  AetherP2pTransport& operator=(AetherP2pTransport const&) = delete;

  void Start(ae::AetherApp& aether_app, ae::Client::ptr local_client);

  // If a session already exists for uid, do nothing. Otherwise CreatePort +
  // P2pStream and subscribe.
  void Connect(ae::Uid const& remote_uid);

  void Send(ae::Uid const& remote_uid, std::uint8_t const* bytes,
            std::size_t size);
  void Send(ae::Uid const& remote_uid,
            std::vector<std::uint8_t> const& bytes);
  void SendText(ae::Uid const& remote_uid, std::string_view text);

  void SetReceiveHandler(ReceiveHandler handler);
  void SetLogHandler(LogHandler handler);
  void SetPreWriteHandler(PreWriteHandler handler);

 private:
  // Runtime only — never serialized. At most one per remote_uid.
  struct PeerSession {
    ae::Uid remote_uid{};
    std::shared_ptr<ae::P2pStream> stream;
    ae::Subscription data_sub;
    AetherP2pFrameDecoder decoder;
  };

  PeerSession* FindSession(ae::Uid const& peer);
  PeerSession const* FindSession(ae::Uid const& peer) const;
  PeerSession* CreateSession(ae::Uid const& peer, ae::P2pPortHandle handle,
                             char const* source);
  PeerSession* GetOrCreateSession(ae::Uid const& peer);

  void AttachIncoming(ae::P2pPortHandle handle);

  void OnRawStreamData(PeerSession* session, ae::DataBuffer const& data);
  void EmitPayload(ae::Uid const& peer,
                   std::vector<std::uint8_t> const& payload);
  void Log(std::string line) const;

  ae::AetherApp* aether_app_{nullptr};
  ae::Client::ptr local_client_;
  ReceiveHandler on_receive_;
  LogHandler on_log_;
  PreWriteHandler on_pre_write_;
  ae::Subscription new_port_sub_;
  std::vector<std::unique_ptr<PeerSession>> sessions_;
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
