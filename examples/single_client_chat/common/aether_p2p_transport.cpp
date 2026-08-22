#include "aether_p2p_transport.h"

#include <algorithm>
#include <utility>

#include "aether/ae_actions/query_peer_ping_schedule.h"
#include "aether-miscpp/format/format.h"

#include "aether_runtime.h"

namespace apptraverse::examples {
namespace {

std::vector<std::uint8_t> ToBytes(std::string_view text) {
  return std::vector<std::uint8_t>(text.begin(), text.end());
}

std::string_view LinkStateName(ae::LinkState state) {
  switch (state) {
    case ae::LinkState::kUnlinked:
      return "unlinked";
    case ae::LinkState::kLinked:
      return "linked";
    case ae::LinkState::kLinkError:
      return "link_error";
  }
  return "unknown";
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

std::string AetherP2pTransport::UidKey(ae::Uid const& uid) {
  return FormatAetherUid(uid);
}

AetherP2pTransport::~AetherP2pTransport() { Stop(); }

void AetherP2pTransport::Stop() {
  new_port_sub_ = ae::Subscription{};
  while (!sessions_.empty()) {
    auto session = std::move(sessions_.back());
    sessions_.pop_back();
    if (session) {
      DestroySessionLocked(*session, "transport_stop");
    }
  }
  reconnect_in_flight_.clear();
  schedule_queries_.clear();
  aether_app_ = nullptr;
  local_client_ = {};
}

void AetherP2pTransport::Start(ae::AetherApp& aether_app,
                               ae::Client::ptr local_client) {
  Stop();
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

void AetherP2pTransport::SetSessionReadyHandler(SessionReadyHandler handler) {
  on_session_ready_ = std::move(handler);
}

void AetherP2pTransport::QueryPeerPingSchedule(
    ae::Uid const& peer, chat::PeerScheduleQueryCallback cb) {
  if (aether_app_ == nullptr || !local_client_.is_valid() || !cb) {
    if (cb) {
      cb(std::nullopt);
    }
    return;
  }
  local_client_.Load();
  if (!local_client_.is_loaded()) {
    cb(std::nullopt);
    return;
  }
  Log(ae::Format("PEER_UAP_QUERY_BEGIN peer={}", peer));
  auto query = std::make_unique<ScheduleQuery>();
  query->cb = std::move(cb);
  query->action = std::make_unique<ae::QueryPeerPingSchedule>(
      ae::AeContext{*aether_app_}, *local_client_, peer);
  auto* raw = query.get();
  query->result_sub = query->action->result_event().Subscribe(
      [raw](ae::Result<ae::PeerPingSchedule, int> const& res) {
        if (!raw->cb) {
          return;
        }
        if (!res) {
          raw->cb(std::nullopt);
          return;
        }
        auto const& in = res.value();
        chat::PeerScheduleSnapshot out{};
        out.last_ping_server_ms = in.last_ping_server_ms;
        out.next_ping_delta_ms = in.next_ping_delta_ms;
        out.local_deadline = in.local_deadline;
        raw->cb(out);
      });
  schedule_queries_.push_back(std::move(query));
}

void AetherP2pTransport::AnnounceNextPingUnknown() {
  if (aether_app_ == nullptr || !local_client_.is_valid()) {
    return;
  }
  local_client_.Load();
  if (!local_client_.is_loaded()) {
    return;
  }
  (void)local_client_->AnnounceNextPingUnknown();
  Log("AETHER_NEXT_PING_UNKNOWN_SENT");
}

std::uint64_t AetherP2pTransport::session_generation(ae::Uid const& peer) const {
  if (auto const* session = FindSession(peer)) {
    return session->generation;
  }
  return 0;
}

std::size_t AetherP2pTransport::live_session_count(ae::Uid const& peer) const {
  std::size_t n = 0;
  for (auto const& session : sessions_) {
    if (session != nullptr && session->remote_uid == peer) {
      ++n;
    }
  }
  return n;
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

void AetherP2pTransport::DestroySessionLocked(PeerSession& session,
                                              char const* reason) {
  Log("P2P_SESSION_DESTROY peer=" + FormatAetherUid(session.remote_uid) +
      " generation=" + std::to_string(session.generation) +
      " reason=" + std::string{reason != nullptr ? reason : "unknown"});
  // Unsubscribe before releasing the stream so late deliveries cannot touch
  // this PeerSession object after erase.
  session.data_sub = ae::Subscription{};
  session.link_sub = ae::Subscription{};
  session.stream.reset();
}

void AetherP2pTransport::NotifySessionReadyWhenWritable(PeerSession& session) {
  if (session.announced_ready || session.stream == nullptr) {
    return;
  }
  auto const info = session.stream->stream_info();
  Log("P2P_STREAM_STATE peer=" + FormatAetherUid(session.remote_uid) +
      " generation=" + std::to_string(session.generation) +
      " link_state=" + std::string{LinkStateName(info.link_state)} +
      " writable=" + std::string{info.is_writable ? "1" : "0"});
  if (info.link_state != ae::LinkState::kLinked) {
    Log("P2P_SESSION_LINK_WAIT peer=" + FormatAetherUid(session.remote_uid) +
        " generation=" + std::to_string(session.generation) +
        " link_state=" + std::string{LinkStateName(info.link_state)});
    return;
  }
  session.announced_ready = true;
  session.link_sub = ae::Subscription{};
  Log("P2P_STREAM_WRITABLE peer=" + FormatAetherUid(session.remote_uid) +
      " generation=" + std::to_string(session.generation));
  Log("P2P_SESSION_WRITABLE peer=" + FormatAetherUid(session.remote_uid) +
      " generation=" + std::to_string(session.generation));
  if (on_session_ready_) {
    on_session_ready_(session.remote_uid, session.source.c_str(),
                      session.generation);
  }
}

AetherP2pTransport::PeerSession* AetherP2pTransport::CreateSession(
    ae::Uid const& peer, ae::P2pPortHandle handle, char const* source) {
  if (!aether_app_ || !local_client_) {
    return nullptr;
  }

  auto const key = UidKey(peer);
  auto& next_gen = next_generation_[key];
  ++next_gen;

  Log("P2P_SESSION_CREATE_BEGIN peer=" + FormatAetherUid(peer) +
      " generation=" + std::to_string(next_gen) +
      " source=" + std::string{source != nullptr ? source : "unknown"});

  auto session = std::make_unique<PeerSession>();
  session->remote_uid = peer;
  session->generation = next_gen;
  session->source = source != nullptr ? source : "unknown";
  session->stream = std::make_shared<ae::P2pStream>(
      *aether_app_, local_client_.Load(), peer, std::move(handle));

  auto const generation = session->generation;
  session->data_sub = session->stream->out_data_event().Subscribe(
      [this, peer, generation](ae::DataBuffer const& data) {
        OnRawStreamData(peer, generation, data);
      });

  Log("P2P_SESSION_CREATE peer=" + FormatAetherUid(peer) +
      " generation=" + std::to_string(generation) +
      " source=" + std::string{source != nullptr ? source : "unknown"});

  sessions_.push_back(std::move(session));
  auto* raw = sessions_.back().get();
  // PeerSession lives in a unique_ptr, so its address is stable; the
  // subscription is owned by the session and released before destruction.
  raw->link_sub = raw->stream->stream_update_event().Subscribe(
      [this, raw]() { NotifySessionReadyWhenWritable(*raw); });
  Log("P2P_SESSION_CREATE_END peer=" + FormatAetherUid(peer) +
      " generation=" + std::to_string(generation));
  NotifySessionReadyWhenWritable(*raw);
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

void AetherP2pTransport::DropSession(ae::Uid const& peer) {
  for (auto it = sessions_.begin(); it != sessions_.end();) {
    if (*it == nullptr || (*it)->remote_uid != peer) {
      ++it;
      continue;
    }
    DestroySessionLocked(**it, "drop");
    it = sessions_.erase(it);
  }
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

void AetherP2pTransport::Reconnect(ae::Uid const& remote_uid) {
  auto const key = UidKey(remote_uid);
  if (!reconnect_in_flight_.insert(key).second) {
    Log("P2P_RECONNECT_SUPPRESSED peer=" + FormatAetherUid(remote_uid) +
        " reason=in_flight");
    return;
  }
  DropSession(remote_uid);
  Connect(remote_uid);
  reconnect_in_flight_.erase(key);
}

void AetherP2pTransport::AttachIncoming(ae::P2pPortHandle handle) {
  auto const peer = handle.destination();
  Log("P2P_ATTACH_INCOMING_BEGIN peer=" + FormatAetherUid(peer) +
      " generation=" + std::to_string(session_generation(peer)));
  if (FindSession(peer) != nullptr) {
    Log("P2P_SESSION_REPLACE_BEGIN peer=" + FormatAetherUid(peer) +
        " source=incoming");
    DropSession(peer);
    Log("P2P_SESSION_REPLACE_END peer=" + FormatAetherUid(peer) +
        " source=incoming");
  }
  (void)CreateSession(peer, std::move(handle), "incoming");
  Log("P2P_ATTACH_INCOMING_END peer=" + FormatAetherUid(peer) +
      " generation=" + std::to_string(session_generation(peer)));
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

void AetherP2pTransport::OnRawStreamData(ae::Uid peer, std::uint64_t generation,
                                         ae::DataBuffer const& data) {
  Log("P2P_STREAM_DATA peer=" + FormatAetherUid(peer) +
      " generation=" + std::to_string(generation) +
      " bytes=" + std::to_string(data.size()));
  auto* session = FindSession(peer);
  if (session == nullptr || session->generation != generation) {
    Log("P2P_SESSION_CALLBACK_STALE_DROPPED peer=" + FormatAetherUid(peer) +
        " generation=" + std::to_string(generation));
    return;
  }
  session->decoder.Append(data.data(), data.size());
  session->decoder.Drain(
      [this, peer, generation](std::vector<std::uint8_t> const& payload) {
        auto* live = FindSession(peer);
        if (live == nullptr || live->generation != generation) {
          Log("P2P_SESSION_CALLBACK_STALE_DROPPED peer=" +
              FormatAetherUid(peer) +
              " generation=" + std::to_string(generation) + " phase=drain");
          return;
        }
        EmitPayload(peer, payload);
      });
}

void AetherP2pTransport::EmitPayload(
    ae::Uid const& peer, std::vector<std::uint8_t> const& payload) {
  Log("P2P_PAYLOAD_RECEIVED peer=" + FormatAetherUid(peer) +
      " bytes=" + std::to_string(payload.size()));
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
