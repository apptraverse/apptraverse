#include "aether_p2p_transport.h"

#include <algorithm>
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

void AetherP2pTransport::Start(ae::AetherApp& aether_app,
                               ae::Client::ptr local_client) {
  aether_app_ = &aether_app;
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

void AetherP2pTransport::SetLogHandler(LogHandler handler) {
  on_log_ = std::move(handler);
}

void AetherP2pTransport::SetPreWriteHandler(PreWriteHandler handler) {
  on_pre_write_ = std::move(handler);
}

AetherP2pTransport::PeerSession* AetherP2pTransport::FindSession(
    ae::Uid const& peer) {
  for (auto& session : sessions_) {
    if (session != nullptr && session->remote_uid == peer) {
      return session.get();
    }
  }
  return nullptr;
}

AetherP2pTransport::PeerSession const* AetherP2pTransport::FindSession(
    ae::Uid const& peer) const {
  for (auto const& session : sessions_) {
    if (session != nullptr && session->remote_uid == peer) {
      return session.get();
    }
  }
  return nullptr;
}

AetherP2pTransport::PeerSession* AetherP2pTransport::CreateSession(
    ae::Uid const& peer, ae::P2pPortHandle handle, char const* source) {
  if (!aether_app_ || !local_client_) {
    return nullptr;
  }

  auto session = std::make_unique<PeerSession>();
  session->remote_uid = peer;
  session->stream = std::make_shared<ae::P2pStream>(
      *aether_app_, local_client_.Load(), peer, std::move(handle));

  auto* raw = session.get();
  session->data_sub = session->stream->out_data_event().Subscribe(
      [this, raw](ae::DataBuffer const& data) { OnRawStreamData(raw, data); });

  sessions_.push_back(std::move(session));
  Log("P2P_SESSION_CREATED peer=" + FormatAetherUid(peer) +
      " source=" + std::string{source != nullptr ? source : "unknown"});
  return raw;
}

AetherP2pTransport::PeerSession* AetherP2pTransport::GetOrCreateSession(
    ae::Uid const& peer) {
  if (auto* existing = FindSession(peer)) {
    return existing;
  }
  Connect(peer);
  return FindSession(peer);
}

void AetherP2pTransport::Connect(ae::Uid const& remote_uid) {
  if (FindSession(remote_uid) != nullptr) {
    return;
  }
  if (!aether_app_ || !local_client_) {
    return;
  }
  auto handle =
      local_client_->message_stream_manager().CreatePort(remote_uid);
  (void)CreateSession(remote_uid, std::move(handle), "connect");
}

void AetherP2pTransport::AttachIncoming(ae::P2pPortHandle handle) {
  auto const peer = handle.destination();
  if (FindSession(peer) != nullptr) {
    // Already have one stream for this peer — do not create a second.
    return;
  }
  (void)CreateSession(peer, std::move(handle), "incoming");
}

void AetherP2pTransport::Send(ae::Uid const& remote_uid,
                              std::vector<std::uint8_t> const& bytes) {
  Send(remote_uid, bytes.data(), bytes.size());
}

void AetherP2pTransport::SendText(ae::Uid const& remote_uid,
                                  std::string_view text) {
  auto const bytes = ToBytes(text);
  Send(remote_uid, bytes.data(), bytes.size());
}

void AetherP2pTransport::Send(ae::Uid const& remote_uid,
                              std::uint8_t const* bytes, std::size_t size) {
  auto* session = GetOrCreateSession(remote_uid);
  if (session == nullptr || session->stream == nullptr) {
    return;
  }
  auto const frame = EncodeAetherP2pFrame(bytes, size);
  if (on_pre_write_) {
    on_pre_write_(remote_uid, frame.size());
  }
  (void)session->stream->Write(ae::DataBuffer{frame.begin(), frame.end()});
}

void AetherP2pTransport::OnRawStreamData(PeerSession* session,
                                         ae::DataBuffer const& data) {
  if (session == nullptr) {
    return;
  }
  session->decoder.Append(data.data(), data.size());
  session->decoder.Drain(
      [this, session](std::vector<std::uint8_t> const& payload) {
        EmitPayload(session->remote_uid, payload);
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
    transport.SendText(peer, kP2pPongPayload);
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
  transport.SendText(peer, kP2pPingPayload);
  if (log_line) {
    log_line("P2P_PING_SENT peer=" + FormatAetherUid(peer));
  }
}

}  // namespace apptraverse::examples
