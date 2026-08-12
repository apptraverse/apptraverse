#include "aether_p2p_transport.h"

#include <utility>

#include "aether_runtime.h"

namespace apptraverse::examples {
namespace {

std::vector<std::uint8_t> ToBytes(std::string_view text) {
  return std::vector<std::uint8_t>(text.begin(), text.end());
}

bool PayloadEquals(std::vector<std::uint8_t> const& payload,
                   std::string_view expected) {
  if (payload.size() != expected.size()) {
    return false;
  }
  for (std::size_t i = 0; i < payload.size(); ++i) {
    if (payload[i] != static_cast<std::uint8_t>(expected[i])) {
      return false;
    }
  }
  return true;
}

}  // namespace

void AetherP2pTransport::Start(ae::RcPtr<ae::AetherApp> aether_app,
                               ae::Client::ptr local_client) {
  aether_app_ = std::move(aether_app);
  local_client_ = std::move(local_client);
  if (!aether_app_ || !local_client_) {
    return;
  }

  new_port_sub_ =
      local_client_->message_stream_manager().new_port_event().Subscribe(
          [this](ae::P2pPortHandle handle) { AttachIncoming(std::move(handle)); });
}

void AetherP2pTransport::SetReceiveHandler(ReceiveHandler handler) {
  on_receive_ = std::move(handler);
}

void AetherP2pTransport::SetOutgoingStateHandler(OutgoingStateHandler handler) {
  on_outgoing_state_ = std::move(handler);
}

void AetherP2pTransport::SetLogHandler(LogHandler handler) {
  on_log_ = std::move(handler);
}

AetherP2pTransport::PeerTransport& AetherP2pTransport::EnsurePeerTransport(
    ae::Uid const& peer) {
  if (auto* existing = FindPeerTransport(peer)) {
    return *existing;
  }
  auto transport = std::make_unique<PeerTransport>();
  transport->remote_uid = peer;
  peers_.push_back(std::move(transport));
  return *peers_.back();
}

AetherP2pTransport::PeerTransport* AetherP2pTransport::FindPeerTransport(
    ae::Uid const& peer) {
  for (auto& transport : peers_) {
    if (transport != nullptr && transport->remote_uid == peer) {
      return transport.get();
    }
  }
  return nullptr;
}

AetherP2pTransport::PeerTransport const*
AetherP2pTransport::FindPeerTransport(ae::Uid const& peer) const {
  for (auto const& transport : peers_) {
    if (transport != nullptr && transport->remote_uid == peer) {
      return transport.get();
    }
  }
  return nullptr;
}

AetherP2pTransport::PeerSession* AetherP2pTransport::FindActiveOutgoing(
    PeerTransport& peer) {
  for (auto& session : peer.sessions) {
    if (session != nullptr &&
        session->direction == P2pStreamDirection::kOutgoing &&
        session->active_for_send) {
      return session.get();
    }
  }
  return nullptr;
}

AetherP2pTransport::PeerSession const* AetherP2pTransport::FindActiveOutgoing(
    PeerTransport const& peer) const {
  for (auto const& session : peer.sessions) {
    if (session != nullptr &&
        session->direction == P2pStreamDirection::kOutgoing &&
        session->active_for_send) {
      return session.get();
    }
  }
  return nullptr;
}

void AetherP2pTransport::RefreshStreamInfo(PeerSession& session) {
  if (session.stream != nullptr) {
    session.info = session.stream->stream_info();
  }
}

P2pOutgoingState AetherP2pTransport::ComputeState(
    PeerSession const* active) const {
  if (active == nullptr) {
    return P2pOutgoingState::kMissing;
  }
  if (active->info.link_state == ae::LinkState::kLinked &&
      active->info.is_writable) {
    return P2pOutgoingState::kWritable;
  }
  if (active->info.link_state == ae::LinkState::kLinkError) {
    return P2pOutgoingState::kError;
  }
  return P2pOutgoingState::kConnecting;
}

AetherP2pTransport::PeerSession* AetherP2pTransport::CreateOutgoingSession(
    PeerTransport& peer) {
  if (!aether_app_ || !local_client_) {
    return nullptr;
  }

  auto handle =
      local_client_->message_stream_manager().CreatePort(peer.remote_uid);

  auto session = std::make_unique<PeerSession>();
  session->peer = peer.remote_uid;
  session->direction = P2pStreamDirection::kOutgoing;
  session->generation = peer.active_outgoing_generation + 1;
  if (session->generation == 0) {
    session->generation = 1;
  }
  session->active_for_send = true;
  session->stream = std::make_shared<ae::P2pStream>(
      *aether_app_, local_client_.Load(), peer.remote_uid, std::move(handle));

  auto* raw = session.get();
  session->data_sub = session->stream->out_data_event().Subscribe(
      [this, raw](ae::DataBuffer const& data) { OnRawStreamData(raw, data); });
  session->stream_update_sub =
      session->stream->stream_update_event().Subscribe(
          [this, raw]() { OnStreamUpdate(raw); });
  RefreshStreamInfo(*raw);

  peer.active_outgoing_generation = raw->generation;
  peer.sessions.push_back(std::move(session));

  if (raw->generation == 1) {
    Log("P2P_OUTGOING_CREATED peer=" + FormatAetherUid(peer.remote_uid) +
        " generation=1");
  }
  PublishOutgoingState(peer);
  return raw;
}

AetherP2pTransport::PeerSession* AetherP2pTransport::CreateIncomingSession(
    PeerTransport& peer, ae::P2pPortHandle handle) {
  if (!aether_app_ || !local_client_) {
    return nullptr;
  }

  auto session = std::make_unique<PeerSession>();
  session->peer = peer.remote_uid;
  session->direction = P2pStreamDirection::kIncoming;
  session->generation = 0;
  session->active_for_send = false;
  session->stream = std::make_shared<ae::P2pStream>(
      *aether_app_, local_client_.Load(), peer.remote_uid, std::move(handle));

  auto* raw = session.get();
  session->data_sub = session->stream->out_data_event().Subscribe(
      [this, raw](ae::DataBuffer const& data) { OnRawStreamData(raw, data); });
  session->stream_update_sub =
      session->stream->stream_update_event().Subscribe(
          [this, raw]() { OnStreamUpdate(raw); });
  RefreshStreamInfo(*raw);

  peer.sessions.push_back(std::move(session));
  return raw;
}

void AetherP2pTransport::EnsureOutgoing(ae::Uid const& peer) {
  auto& transport = EnsurePeerTransport(peer);
  if (FindActiveOutgoing(transport) != nullptr) {
    return;
  }
  (void)CreateOutgoingSession(transport);
}

void AetherP2pTransport::Connect(ae::Uid const& remote_uid) {
  EnsureOutgoing(remote_uid);
}

void AetherP2pTransport::RestreamOutgoing(ae::Uid const& peer) {
  auto* transport = FindPeerTransport(peer);
  if (transport == nullptr) {
    return;
  }
  auto* active = FindActiveOutgoing(*transport);
  if (active == nullptr || active->stream == nullptr) {
    return;
  }
  RefreshStreamInfo(*active);
  // Soft-skip: Restream() on a linked+writable stream races reverse-path ACKs
  // and has been observed to break Android→Windows delivery while the peer
  // already applied the pending packet. Error/non-writable paths still Restream.
  if (active->info.link_state == ae::LinkState::kLinked &&
      active->info.is_writable) {
    Log("P2P_OUTGOING_RESTREAM peer=" + FormatAetherUid(peer) +
        " generation=" + std::to_string(active->generation) +
        " skipped=writable");
    return;
  }
  active->stream->Restream();
  Log("P2P_OUTGOING_RESTREAM peer=" + FormatAetherUid(peer) +
      " generation=" + std::to_string(active->generation));
  RefreshStreamInfo(*active);
  PublishOutgoingState(*transport);
}

void AetherP2pTransport::ReplaceOutgoing(ae::Uid const& peer) {
  auto& transport = EnsurePeerTransport(peer);
  auto* old_active = FindActiveOutgoing(transport);
  std::uint64_t old_generation = 0;
  if (old_active != nullptr) {
    old_generation = old_active->generation;
    old_active->active_for_send = false;
  }
  auto* created = CreateOutgoingSession(transport);
  if (created == nullptr) {
    return;
  }
  Log("P2P_OUTGOING_REPLACED peer=" + FormatAetherUid(peer) +
      " old_generation=" + std::to_string(old_generation) +
      " new_generation=" + std::to_string(created->generation));
}

void AetherP2pTransport::AttachIncoming(ae::P2pPortHandle handle) {
  auto const peer = handle.destination();
  auto& transport = EnsurePeerTransport(peer);
  // Incoming streams are receive-only. Create the local outgoing path before
  // any payload / ACK processing that may follow on this port.
  (void)CreateIncomingSession(transport, std::move(handle));
  EnsureOutgoing(peer);
}

P2pSendResult AetherP2pTransport::Send(ae::Uid const& remote_uid,
                                       std::vector<std::uint8_t> const& bytes) {
  return Send(remote_uid, bytes.data(), bytes.size());
}

P2pSendResult AetherP2pTransport::SendText(ae::Uid const& remote_uid,
                                           std::string_view text) {
  auto const bytes = ToBytes(text);
  return Send(remote_uid, bytes.data(), bytes.size());
}

P2pSendResult AetherP2pTransport::Send(ae::Uid const& remote_uid,
                                       std::uint8_t const* bytes,
                                       std::size_t size) {
  EnsureOutgoing(remote_uid);
  auto* transport = FindPeerTransport(remote_uid);
  P2pSendResult result;
  if (transport == nullptr) {
    return result;
  }
  auto* active = FindActiveOutgoing(*transport);
  if (active == nullptr || active->stream == nullptr) {
    return result;
  }

  RefreshStreamInfo(*active);
  result.outgoing_generation = active->generation;
  result.disposition =
      (active->info.link_state == ae::LinkState::kLinked &&
       active->info.is_writable)
          ? P2pSendDisposition::kWritable
          : P2pSendDisposition::kBufferedOrConnecting;

  auto const frame = EncodeAetherP2pFrame(bytes, size);
  (void)active->stream->Write(ae::DataBuffer{frame.begin(), frame.end()});
  return result;
}

P2pOutgoingState AetherP2pTransport::OutgoingState(
    ae::Uid const& remote_uid) const {
  auto const* transport = FindPeerTransport(remote_uid);
  if (transport == nullptr) {
    return P2pOutgoingState::kMissing;
  }
  auto const* active = FindActiveOutgoing(*transport);
  if (active == nullptr) {
    return P2pOutgoingState::kMissing;
  }
  ae::StreamInfo info = active->info;
  if (active->stream != nullptr) {
    info = active->stream->stream_info();
  }
  if (info.link_state == ae::LinkState::kLinked && info.is_writable) {
    return P2pOutgoingState::kWritable;
  }
  if (info.link_state == ae::LinkState::kLinkError) {
    return P2pOutgoingState::kError;
  }
  return P2pOutgoingState::kConnecting;
}

bool AetherP2pTransport::IsOutgoingWritable(ae::Uid const& remote_uid) const {
  return OutgoingState(remote_uid) == P2pOutgoingState::kWritable;
}

void AetherP2pTransport::OnStreamUpdate(PeerSession* session) {
  if (session == nullptr) {
    return;
  }
  RefreshStreamInfo(*session);
  auto* transport = FindPeerTransport(session->peer);
  if (transport == nullptr) {
    return;
  }
  // Only active outgoing state is published. Incoming / retired updates are
  // ignored for outgoing diagnostics.
  if (session->direction == P2pStreamDirection::kOutgoing &&
      session->active_for_send) {
    PublishOutgoingState(*transport);
  }
}

void AetherP2pTransport::PublishOutgoingState(PeerTransport& peer) {
  auto* active = FindActiveOutgoing(peer);
  if (active != nullptr) {
    RefreshStreamInfo(*active);
  }
  auto const state = ComputeState(active);
  if (peer.last_published_outgoing_state == state) {
    return;
  }
  peer.last_published_outgoing_state = state;
  auto const generation =
      active != nullptr ? active->generation : peer.active_outgoing_generation;
  Log("P2P_OUTGOING_STATE peer=" + FormatAetherUid(peer.remote_uid) +
      " generation=" + std::to_string(generation) +
      " state=" + std::string{ToString(state)});
  if (on_outgoing_state_) {
    on_outgoing_state_(peer.remote_uid, state);
  }
}

void AetherP2pTransport::OnRawStreamData(PeerSession* session,
                                         ae::DataBuffer const& data) {
  if (session == nullptr) {
    return;
  }
  // Receive activity never influences which stream is used for Send.
  session->decoder.Append(data.data(), data.size());
  session->decoder.Drain(
      [this, session](std::vector<std::uint8_t> const& payload) {
        EmitPayload(session->peer, payload);
      });
}

void AetherP2pTransport::EmitPayload(
    ae::Uid const& peer, std::vector<std::uint8_t> const& payload) {
  if (on_receive_) {
    on_receive_(peer, payload);
  }
}

void AetherP2pTransport::Log(std::string line) const {
  if (on_log_) {
    on_log_(std::move(line));
  }
}

bool TryHandleP2pProbePayload(
    AetherP2pTransport& transport, ae::Uid const& peer,
    std::vector<std::uint8_t> const& payload,
    std::function<void(std::string const&)> const& log_line,
    std::function<void()> const& on_pong_received) {
  auto const peer_text = FormatAetherUid(peer);
  if (PayloadEquals(payload, kP2pPingPayload)) {
    if (log_line) {
      log_line("P2P_PING_RECEIVED peer=" + peer_text);
    }
    (void)transport.SendText(peer, kP2pPongPayload);
    if (log_line) {
      log_line("P2P_PONG_SENT peer=" + peer_text);
    }
    return true;
  }
  if (PayloadEquals(payload, kP2pPongPayload)) {
    if (log_line) {
      log_line("P2P_PONG_RECEIVED peer=" + peer_text);
    }
    if (on_pong_received) {
      on_pong_received();
    }
    return true;
  }
  return false;
}

void AttachPingPongProbe(AetherP2pTransport& transport,
                         std::function<void(std::string const&)> log_line,
                         std::function<void()> on_pong_received) {
  transport.SetReceiveHandler(
      [&transport, log_line = std::move(log_line),
       on_pong_received = std::move(on_pong_received)](
          ae::Uid const& peer, std::vector<std::uint8_t> const& payload) {
        (void)TryHandleP2pProbePayload(transport, peer, payload, log_line,
                                       on_pong_received);
      });
}

void SendP2pPing(AetherP2pTransport& transport, ae::Uid const& peer,
                 std::function<void(std::string const&)> log_line) {
  transport.Connect(peer);
  (void)transport.SendText(peer, kP2pPingPayload);
  if (log_line) {
    log_line("P2P_PING_SENT peer=" + FormatAetherUid(peer));
  }
}

}  // namespace apptraverse::examples
