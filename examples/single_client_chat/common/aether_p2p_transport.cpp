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

void AetherP2pTransport::Connect(ae::Uid const& remote_uid) {
  (void)EnsureOutgoingSession(remote_uid);
}

void AetherP2pTransport::Send(ae::Uid const& remote_uid,
                              std::vector<std::uint8_t> const& bytes) {
  Send(remote_uid, bytes.data(), bytes.size());
}

void AetherP2pTransport::SendText(ae::Uid const& remote_uid,
                                  std::string_view text) {
  auto bytes = ToBytes(text);
  Send(remote_uid, bytes.data(), bytes.size());
}

void AetherP2pTransport::Send(ae::Uid const& remote_uid,
                              std::uint8_t const* bytes, std::size_t size) {
  auto* session = EnsureOutgoingSession(remote_uid);
  if (session == nullptr || session->stream == nullptr) {
    return;
  }
  auto frame = EncodeAetherP2pFrame(bytes, size);
  (void)session->stream->Write(
      ae::DataBuffer{frame.begin(), frame.end()});
}

AetherP2pTransport::PeerSession* AetherP2pTransport::FindSession(
    ae::Uid const& remote_uid) {
  auto it = sessions_.find(remote_uid);
  if (it == sessions_.end()) {
    return nullptr;
  }
  return it->second.get();
}

AetherP2pTransport::PeerSession*
AetherP2pTransport::EnsureOutgoingSession(ae::Uid const& remote_uid) {
  if (auto* existing = FindSession(remote_uid)) {
    return existing;
  }
  if (!aether_app_ || !local_client_) {
    return nullptr;
  }

  auto handle =
      local_client_->message_stream_manager().CreatePort(remote_uid);
  auto session = std::make_unique<PeerSession>();
  session->peer = remote_uid;
  session->stream = std::make_shared<ae::P2pStream>(
      *aether_app_, local_client_.Load(), remote_uid, std::move(handle));
  auto* raw = session.get();
  session->data_sub = session->stream->out_data_event().Subscribe(
      [this, raw](ae::DataBuffer const& data) { OnRawStreamData(raw, data); });
  auto* inserted = session.get();
  sessions_.emplace(remote_uid, std::move(session));
  return inserted;
}

void AetherP2pTransport::AttachIncoming(ae::P2pPortHandle handle) {
  if (!aether_app_ || !local_client_) {
    return;
  }
  auto const peer = handle.destination();
  if (FindSession(peer) != nullptr) {
    return;
  }

  auto session = std::make_unique<PeerSession>();
  session->peer = peer;
  session->stream = std::make_shared<ae::P2pStream>(
      *aether_app_, local_client_.Load(), peer, std::move(handle));
  auto* raw = session.get();
  session->data_sub = session->stream->out_data_event().Subscribe(
      [this, raw](ae::DataBuffer const& data) { OnRawStreamData(raw, data); });
  sessions_.emplace(peer, std::move(session));
}

void AetherP2pTransport::OnRawStreamData(PeerSession* session,
                                         ae::DataBuffer const& data) {
  if (session == nullptr) {
    return;
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

void AttachPingPongProbe(AetherP2pTransport& transport,
                         std::function<void(std::string const&)> log_line,
                         std::function<void()> on_pong_received) {
  transport.SetReceiveHandler(
      [&transport, log_line = std::move(log_line),
       on_pong_received = std::move(on_pong_received)](
          ae::Uid const& peer, std::vector<std::uint8_t> const& payload) {
        auto const peer_text = FormatAetherUid(peer);
        if (PayloadEquals(payload, kP2pPingPayload)) {
          if (log_line) {
            log_line("P2P_PING_RECEIVED peer=" + peer_text);
          }
          transport.SendText(peer, kP2pPongPayload);
          if (log_line) {
            log_line("P2P_PONG_SENT peer=" + peer_text);
          }
          return;
        }
        if (PayloadEquals(payload, kP2pPongPayload)) {
          if (log_line) {
            log_line("P2P_PONG_RECEIVED peer=" + peer_text);
          }
          if (on_pong_received) {
            on_pong_received();
          }
        }
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
