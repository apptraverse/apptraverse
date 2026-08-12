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

P2pSessionSelectCandidate ToCandidate(std::uint64_t creation_order,
                                      std::uint64_t last_receive_order,
                                      ae::StreamInfo const& info) {
  P2pSessionSelectCandidate candidate;
  candidate.creation_order = creation_order;
  candidate.last_receive_order = last_receive_order;
  candidate.is_writable =
      info.link_state == ae::LinkState::kLinked && info.is_writable;
  candidate.is_link_error = info.link_state == ae::LinkState::kLinkError;
  return candidate;
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

void AetherP2pTransport::SetPeerStateHandler(PeerStateHandler handler) {
  on_peer_state_ = std::move(handler);
}

void AetherP2pTransport::SetLogHandler(LogHandler handler) {
  on_log_ = std::move(handler);
}

void AetherP2pTransport::Connect(ae::Uid const& remote_uid) {
  auto sessions = CollectPeerSessions(remote_uid);
  if (sessions.empty()) {
    (void)CreateOutgoingSession(remote_uid);
    return;
  }

  auto const candidates = BuildPeerCandidates(sessions);
  auto const state = AggregatePeerTransportState(candidates);
  if (state == P2pPeerTransportState::kWritable) {
    return;
  }
  // A still-connecting stream is already negotiating — Restream() there can
  // tear down an in-flight first Write (AddPeer/Send then Connect). Only
  // revive streams that are in terminal link error; never open another
  // outgoing for the same peer from Connect.
  if (state != P2pPeerTransportState::kError) {
    return;
  }

  auto const best = SelectBestP2pSession(candidates);
  if (best.has_value() && sessions[best->index]->stream != nullptr) {
    sessions[best->index]->stream->Restream();
  }
}

P2pSendDisposition AetherP2pTransport::Send(
    ae::Uid const& remote_uid, std::vector<std::uint8_t> const& bytes) {
  return Send(remote_uid, bytes.data(), bytes.size());
}

P2pSendDisposition AetherP2pTransport::SendText(ae::Uid const& remote_uid,
                                                std::string_view text) {
  auto const bytes = ToBytes(text);
  return Send(remote_uid, bytes.data(), bytes.size());
}

P2pSendDisposition AetherP2pTransport::Send(ae::Uid const& remote_uid,
                                            std::uint8_t const* bytes,
                                            std::size_t size) {
  auto sessions = CollectPeerSessions(remote_uid);
  auto candidates = BuildPeerCandidates(sessions);
  auto best = SelectBestP2pSession(candidates);

  if (!best.has_value()) {
    auto* created = CreateOutgoingSession(remote_uid);
    if (created == nullptr) {
      return P2pSendDisposition::kBufferedOrConnecting;
    }
    sessions = CollectPeerSessions(remote_uid);
    candidates = BuildPeerCandidates(sessions);
    best = SelectBestP2pSession(candidates);
    if (!best.has_value()) {
      return P2pSendDisposition::kBufferedOrConnecting;
    }
  }

  auto* session = sessions[best->index];
  if (session == nullptr || session->stream == nullptr) {
    return P2pSendDisposition::kBufferedOrConnecting;
  }

  auto& tracking = Tracking(remote_uid);
  bool const selection_changed =
      !tracking.has_selected ||
      tracking.last_selected_creation_order != session->creation_order ||
      tracking.last_select_reason != best->reason;
  if (selection_changed) {
    tracking.has_selected = true;
    tracking.last_selected_creation_order = session->creation_order;
    tracking.last_select_reason = best->reason;
    Log("P2P_STREAM_SELECTED peer=" + FormatAetherUid(remote_uid) +
        " direction=" + std::string{ToString(session->direction)} +
        " reason=" + std::string{ToString(best->reason)});
  }

  auto const frame = EncodeAetherP2pFrame(bytes, size);
  (void)session->stream->Write(ae::DataBuffer{frame.begin(), frame.end()});

  return candidates[best->index].is_writable
             ? P2pSendDisposition::kWritable
             : P2pSendDisposition::kBufferedOrConnecting;
}

P2pPeerTransportState AetherP2pTransport::PeerState(
    ae::Uid const& remote_uid) const {
  std::vector<P2pSessionSelectCandidate> candidates;
  for (auto const& session : sessions_) {
    if (session == nullptr || !(session->peer == remote_uid)) {
      continue;
    }
    auto const info =
        session->stream != nullptr ? session->stream->stream_info() : session->info;
    candidates.push_back(ToCandidate(session->creation_order,
                                     session->last_receive_order, info));
  }
  return AggregatePeerTransportState(candidates);
}

bool AetherP2pTransport::IsPeerWritable(ae::Uid const& remote_uid) const {
  return PeerState(remote_uid) == P2pPeerTransportState::kWritable;
}

std::vector<AetherP2pTransport::PeerSession*>
AetherP2pTransport::CollectPeerSessions(ae::Uid const& peer) {
  std::vector<PeerSession*> found;
  for (auto& session : sessions_) {
    if (session != nullptr && session->peer == peer) {
      found.push_back(session.get());
    }
  }
  return found;
}

std::vector<P2pSessionSelectCandidate> AetherP2pTransport::BuildPeerCandidates(
    std::vector<PeerSession*> const& sessions) {
  std::vector<P2pSessionSelectCandidate> candidates;
  candidates.reserve(sessions.size());
  for (auto* session : sessions) {
    RefreshStreamInfo(*session);
    candidates.push_back(ToCandidate(session->creation_order,
                                     session->last_receive_order,
                                     session->info));
  }
  return candidates;
}

void AetherP2pTransport::RefreshStreamInfo(PeerSession& session) {
  if (session.stream != nullptr) {
    session.info = session.stream->stream_info();
  }
}

AetherP2pTransport::PeerSession* AetherP2pTransport::CreateSession(
    ae::Uid const& peer, ae::P2pPortHandle handle,
    P2pStreamDirection direction) {
  if (!aether_app_ || !local_client_) {
    return nullptr;
  }

  auto session = std::make_unique<PeerSession>();
  session->peer = peer;
  session->direction = direction;
  session->creation_order = next_creation_order_++;
  session->stream = std::make_shared<ae::P2pStream>(
      *aether_app_, local_client_.Load(), peer, std::move(handle));

  auto* raw = session.get();
  session->data_sub = session->stream->out_data_event().Subscribe(
      [this, raw](ae::DataBuffer const& data) { OnRawStreamData(raw, data); });
  session->stream_update_sub =
      session->stream->stream_update_event().Subscribe(
          [this, raw]() { OnStreamUpdate(raw); });
  RefreshStreamInfo(*raw);

  sessions_.push_back(std::move(session));
  PublishPeerState(peer);
  return raw;
}

AetherP2pTransport::PeerSession* AetherP2pTransport::CreateOutgoingSession(
    ae::Uid const& peer) {
  if (!aether_app_ || !local_client_) {
    return nullptr;
  }
  auto handle = local_client_->message_stream_manager().CreatePort(peer);
  return CreateSession(peer, std::move(handle), P2pStreamDirection::kOutgoing);
}

void AetherP2pTransport::AttachIncoming(ae::P2pPortHandle handle) {
  auto const peer = handle.destination();
  // An incoming port is always accepted and appended: the remote side may have
  // given up on the stream we hold, and selection decides which one we use.
  (void)CreateSession(peer, std::move(handle), P2pStreamDirection::kIncoming);
}

void AetherP2pTransport::OnStreamUpdate(PeerSession* session) {
  if (session == nullptr) {
    return;
  }
  RefreshStreamInfo(*session);
  PublishPeerState(session->peer);
}

void AetherP2pTransport::PublishPeerState(ae::Uid const& peer) {
  auto const sessions = CollectPeerSessions(peer);
  std::vector<P2pSessionSelectCandidate> candidates;
  candidates.reserve(sessions.size());
  for (auto const* session : sessions) {
    candidates.push_back(ToCandidate(session->creation_order,
                                     session->last_receive_order,
                                     session->info));
  }

  auto const state = AggregatePeerTransportState(candidates);
  auto& tracking = Tracking(peer);
  if (tracking.last_state == state) {
    return;
  }
  tracking.last_state = state;
  Log("P2P_PEER_STATE peer=" + FormatAetherUid(peer) +
      " state=" + std::string{ToString(state)});
  if (on_peer_state_) {
    on_peer_state_(peer, state);
  }
}

AetherP2pTransport::PeerTracking& AetherP2pTransport::Tracking(
    ae::Uid const& peer) {
  for (auto& tracking : peer_tracking_) {
    if (tracking.peer == peer) {
      return tracking;
    }
  }
  PeerTracking tracking;
  tracking.peer = peer;
  peer_tracking_.push_back(tracking);
  return peer_tracking_.back();
}

void AetherP2pTransport::OnRawStreamData(PeerSession* session,
                                         ae::DataBuffer const& data) {
  if (session == nullptr) {
    return;
  }
  // Recorded before decoding so a reply produced by the receive handler already
  // prefers the stream the peer is actually talking on.
  session->last_receive_order = next_receive_order_++;
  auto& tracking = Tracking(session->peer);
  bool const preference_changed =
      !tracking.has_selected ||
      tracking.last_selected_creation_order != session->creation_order ||
      tracking.last_select_reason != P2pStreamSelectReason::kRecentReceive;
  if (preference_changed) {
    tracking.has_selected = true;
    tracking.last_selected_creation_order = session->creation_order;
    tracking.last_select_reason = P2pStreamSelectReason::kRecentReceive;
    Log("P2P_STREAM_SELECTED peer=" + FormatAetherUid(session->peer) +
        " direction=" + std::string(ToString(session->direction)) +
        " reason=recent_receive");
  }

  session->decoder.Append(data.data(), data.size());
  session->decoder.Drain([this, session](std::vector<std::uint8_t> const& payload) {
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
