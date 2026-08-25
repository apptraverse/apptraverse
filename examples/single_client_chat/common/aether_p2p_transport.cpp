#include "aether_p2p_transport.h"

#include <algorithm>
#include <cassert>
#include <utility>

#include "aether-miscpp/format/format.h"

#include "apptraverse/runtime_trace.h"

#include "aether_runtime.h"
#include "chat_peer_schedule.h"
#include "room_control.h"
#include "startup_trace.h"

namespace apptraverse::examples {
namespace {

std::vector<std::uint8_t> ToBytes(std::string_view text) {
  return std::vector<std::uint8_t>(text.begin(), text.end());
}

std::string ClassifyPayload(std::uint8_t const* bytes, std::size_t size) {
  auto decoded = chat::TryDecodeRoomControl(bytes, size);
  if (!decoded.has_value()) {
    return "other";
  }
  switch (decoded->type) {
    case chat::RoomControlType::kJoinRoomRequest:
      return "join_request";
    case chat::RoomControlType::kJoinRoomAccepted:
      return "join_accepted";
    case chat::RoomControlType::kJoinRoomRejected:
      return "join_rejected";
    case chat::RoomControlType::kParticipantsChanged:
      return "participants_changed";
    case chat::RoomControlType::kJoinRoomAcceptedAck:
      return "join_accepted_ack";
  }
  return "other";
}

std::string ClassifyPayload(std::vector<std::uint8_t> const& bytes) {
  return ClassifyPayload(bytes.data(), bytes.size());
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
  stopped_ = true;
  prepare_for_shutdown_done_ = true;
  new_port_sub_ = ae::Subscription{};
  while (!sessions_.empty()) {
    auto session = std::move(sessions_.back());
    sessions_.pop_back();
    if (session) {
      DestroySessionLocked(*session, "transport_stop", true);
    }
  }
  reconnect_in_flight_.clear();
  schedule_query_phase_ = ScheduleQueryPhase::Idle;
  schedule_result_sub_ = ae::Subscription{};
  schedule_query_completed_ = false;
  next_presence_cycle_ = {};
  flushed_frame_ids_.clear();
  aether_app_ = nullptr;
  local_client_ = {};
}

void AetherP2pTransport::Start(ae::AetherApp& aether_app,
                               ae::Client::ptr local_client) {
  AssertNetworkThread("AetherP2pTransport::Start");
  Stop();
  stopped_ = false;
  aether_app_ = &aether_app;
  local_client_ = std::move(local_client);
  if (!aether_app_ || !local_client_) {
    return;
  }

  new_port_sub_ =
      local_client_->message_stream_manager().new_port_event().Subscribe(
          [this](ae::P2pPortHandle handle) {
            AssertNetworkThread("AetherP2pTransport::new_port");
            AttachIncoming(std::move(handle));
          });
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

void AetherP2pTransport::SetPresenceUids(ae::Uid local_uid, ae::Uid remote_uid) {
  AssertNetworkThread("AetherP2pTransport::SetPresenceUids");
  local_uid_ = std::move(local_uid);
  remote_uid_ = std::move(remote_uid);
}

void AetherP2pTransport::SetScheduleResultHandler(ScheduleResultHandler handler) {
  on_schedule_result_ = std::move(handler);
}

void AetherP2pTransport::SetWakeNetwork(std::function<void()> wake) {
  wake_network_ = std::move(wake);
}

void AetherP2pTransport::PollPresence(ae::TimePoint now) {
  AssertNetworkThread("AetherP2pTransport::PollPresence");
  if (stopped_ || schedule_query_phase_ != ScheduleQueryPhase::Idle) {
    return;
  }
  if (now < next_presence_cycle_) {
    return;
  }
  StartLocalQuery();
}

void AetherP2pTransport::ContinuePresenceCycle() {
  AssertNetworkThread("AetherP2pTransport::ContinuePresenceCycle");
  if (stopped_) {
    schedule_query_phase_ = ScheduleQueryPhase::Idle;
    schedule_result_sub_ = ae::Subscription{};
    schedule_query_completed_ = false;
    return;
  }
  auto const phase = schedule_query_phase_;
  schedule_result_sub_ = ae::Subscription{};
  schedule_query_completed_ = false;
  if (phase == ScheduleQueryPhase::LocalInFlight) {
    if (!remote_uid_.empty()) {
      schedule_query_phase_ = ScheduleQueryPhase::Idle;
      StartRemoteQuery();
      return;
    }
    schedule_query_phase_ = ScheduleQueryPhase::Idle;
    next_presence_cycle_ = ae::Now() + chat::kPresenceRefreshInterval;
    return;
  }
  if (phase == ScheduleQueryPhase::RemoteInFlight) {
    schedule_query_phase_ = ScheduleQueryPhase::Idle;
    next_presence_cycle_ = ae::Now() + chat::kPresenceRefreshInterval;
  }
}

void AetherP2pTransport::SubscribeScheduleResult(ae::Uid peer, bool is_local) {
  if (!local_client_.is_valid() || !local_client_.is_loaded()) {
    schedule_query_completed_ = true;
    if (on_schedule_result_) {
      on_schedule_result_(peer, is_local, std::nullopt);
    }
    if (wake_network_) {
      wake_network_();
    }
    return;
  }
  auto& action = local_client_->QueryPeerReceiveSchedule(peer);
  schedule_query_completed_ = false;
  schedule_result_sub_ = action.result_event().Subscribe(
      [this, peer, is_local](
          ae::Result<ae::PeerReceiveSchedule, int> const& res) {
        if (stopped_ || schedule_query_completed_) {
          return;
        }
        schedule_query_completed_ = true;
        std::optional<chat::PeerScheduleSnapshot> snapshot;
        if (!res) {
          if (StartupOnceFlag(StartupFlagFirstCloudResponse())) {
            StartupTrace(
                "FIRST_CLOUD_RESPONSE",
                ae::Format("kind=QueryPeerReceiveSchedule success=0 peer={}",
                           peer));
          }
          snapshot = std::nullopt;
        } else {
          if (StartupOnceFlag(StartupFlagFirstCloudResponse())) {
            auto const& schedule = res.value();
            StartupTrace(
                "FIRST_CLOUD_RESPONSE",
                ae::Format(
                    "kind=QueryPeerReceiveSchedule success=1 peer={} "
                    "schedule_state={}",
                    peer, static_cast<int>(schedule.state)));
          }
          snapshot = chat::MakePeerScheduleSnapshot(res.value());
        }
        // Copy result + notify only — never start next query or reset transport.
        if (on_schedule_result_) {
          on_schedule_result_(peer, is_local, std::move(snapshot));
        }
        if (wake_network_) {
          wake_network_();
        }
      });
}

void AetherP2pTransport::StartLocalQuery() {
  assert(schedule_query_phase_ == ScheduleQueryPhase::Idle);
  if (stopped_ || aether_app_ == nullptr || !local_client_.is_valid() ||
      local_uid_.empty()) {
    next_presence_cycle_ = ae::Now() + chat::kPresenceRefreshInterval;
    return;
  }
  local_client_.Load();
  if (!local_client_.is_loaded()) {
    next_presence_cycle_ = ae::Now() + chat::kPresenceRefreshInterval;
    return;
  }
  if (StartupOnceFlag(StartupFlagFirstNetworkEnq())) {
    StartupTrace(
        "FIRST_NETWORK_REQUEST_ENQUEUED",
        ae::Format("kind=QueryPeerReceiveSchedule peer=local uid={}",
                   local_uid_));
  }
  Log(ae::Format("PEER_SCHEDULE_QUERY_BEGIN peer={} phase=local", local_uid_));
  schedule_query_phase_ = ScheduleQueryPhase::LocalInFlight;
  SubscribeScheduleResult(local_uid_, true);
}

void AetherP2pTransport::StartRemoteQuery() {
  assert(schedule_query_phase_ == ScheduleQueryPhase::Idle);
  if (stopped_ || aether_app_ == nullptr || !local_client_.is_valid() ||
      remote_uid_.empty()) {
    schedule_query_phase_ = ScheduleQueryPhase::Idle;
    next_presence_cycle_ = ae::Now() + chat::kPresenceRefreshInterval;
    return;
  }
  local_client_.Load();
  if (!local_client_.is_loaded()) {
    schedule_query_phase_ = ScheduleQueryPhase::Idle;
    next_presence_cycle_ = ae::Now() + chat::kPresenceRefreshInterval;
    return;
  }
  Log(ae::Format("PEER_SCHEDULE_QUERY_BEGIN peer={} phase=remote",
                 remote_uid_));
  schedule_query_phase_ = ScheduleQueryPhase::RemoteInFlight;
  SubscribeScheduleResult(remote_uid_, false);
}

void AetherP2pTransport::QueryPeerReceiveSchedule(
    ae::Uid const& peer, chat::PeerScheduleQueryCallback cb) {
  (void)peer;
  // Ad-hoc concurrent queries removed — Client keeps one QueryPeerReceiveSchedule.
  // Presence uses PollPresence / ContinuePresenceCycle only.
  assert(schedule_query_phase_ == ScheduleQueryPhase::Idle);
  if (cb) {
    cb(std::nullopt);
  }
}

void AetherP2pTransport::AnnounceNextPingUnknown() {}

bool AetherP2pTransport::BeginPrepareForShutdown() {
  prepare_for_shutdown_done_ = true;
  return false;
}

bool AetherP2pTransport::PollPrepareForShutdown() {
  return prepare_for_shutdown_done_;
}

std::size_t AetherP2pTransport::active_schedule_query_count() const {
  return schedule_query_phase_ == ScheduleQueryPhase::Idle ? 0 : 1;
}

void AetherP2pTransport::Poll() {
  AssertNetworkThread("AetherP2pTransport::Poll");
  for (auto& session : sessions_) {
    if (session != nullptr && !session->pending_frames.empty() &&
        StreamWritable(*session)) {
      // Drain any frames queued after SessionReady while briefly non-writable.
      FlushPendingFrames(*session, false);
    }
  }
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

std::uint64_t AetherP2pTransport::AllocFrameId() { return next_frame_id_++; }

AetherP2pTransport::PeerSession* AetherP2pTransport::FindSession(
    ae::Uid const& peer) {
  for (auto& session : sessions_) {
    if (session != nullptr && session->remote_uid == peer) {
      return session.get();
    }
  }
  return nullptr;
}

AetherP2pTransport::PeerSession* AetherP2pTransport::FindSession(
    ae::Uid const& peer, std::uint64_t generation) {
  for (auto& session : sessions_) {
    if (session != nullptr && session->remote_uid == peer &&
        session->generation == generation) {
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

std::deque<AetherP2pTransport::PendingFrame>
AetherP2pTransport::TakePendingFrames(PeerSession& session) {
  std::deque<PendingFrame> out = std::move(session.pending_frames);
  session.pending_frames.clear();
  return out;
}

void AetherP2pTransport::DestroySessionLocked(PeerSession& session,
                                              char const* reason,
                                              bool clear_pending) {
  Trace("P2P_SESSION_DESTROY",
        ae::Format("peer={} generation={} pending={} clear_pending={} reason={}",
                   session.remote_uid, session.generation,
                   session.pending_frames.size(), clear_pending ? 1 : 0,
                   reason != nullptr ? reason : "unknown"));
  Log("P2P_SESSION_DESTROY peer=" + FormatAetherUid(session.remote_uid) +
      " generation=" + std::to_string(session.generation) +
      " pending=" + std::to_string(session.pending_frames.size()) +
      " clear_pending=" + std::string{clear_pending ? "1" : "0"} +
      " reason=" + std::string{reason != nullptr ? reason : "unknown"});
  session.data_sub = ae::Subscription{};
  session.link_sub = ae::Subscription{};
  if (clear_pending) {
    session.pending_frames.clear();
  }
  session.stream.reset();
}

bool AetherP2pTransport::StreamWritable(PeerSession const& session) const {
  if (session.stream == nullptr) {
    return false;
  }
  auto const info = session.stream->stream_info();
  return info.link_state == ae::LinkState::kLinked && info.is_writable;
}

void AetherP2pTransport::FlushPendingFrames(PeerSession& session,
                                            bool from_session_ready) {
  AssertNetworkThread("AetherP2pTransport::FlushPendingFrames");
  while (!session.pending_frames.empty()) {
    if (!StreamWritable(session)) {
      Log("P2P_FRAME_FLUSH_WAIT peer=" + FormatAetherUid(session.remote_uid) +
          " generation=" + std::to_string(session.generation) +
          " pending=" + std::to_string(session.pending_frames.size()));
      return;
    }
    auto frame = std::move(session.pending_frames.front());
    session.pending_frames.pop_front();
    Trace("FRAME_FIFO_FLUSHED",
          ae::Format("id={} peer={} generation={} kind={} size={}", frame.id,
                     session.remote_uid, session.generation, frame.kind,
                     frame.bytes.size()));
    if (frame.kind == "join_request") {
      Trace("JOIN_REQUEST_FIFO_FLUSHED",
            ae::Format("id={} peer={} generation={}", frame.id,
                       session.remote_uid, session.generation));
    } else if (frame.kind == "join_accepted") {
      Trace("JOIN_ACCEPTED_FIFO_FLUSHED",
            ae::Format("id={} peer={} generation={}", frame.id,
                       session.remote_uid, session.generation));
    }
    if (!flushed_frame_ids_.insert(frame.id).second) {
      Trace("FRAME_DUPLICATE_WRITE",
            ae::Format("id={} peer={} generation={}", frame.id,
                       session.remote_uid, session.generation));
      Log("NETWORK_FRAME_DUPLICATE_SUPPRESSED id=" + std::to_string(frame.id) +
          " peer=" + FormatAetherUid(session.remote_uid) +
          " generation=" + std::to_string(session.generation));
      continue;
    }
    Log("NETWORK_FRAME_FLUSHED id=" + std::to_string(frame.id) +
        " peer=" + FormatAetherUid(session.remote_uid) +
        " generation=" + std::to_string(session.generation) +
        " bytes=" + std::to_string(frame.bytes.size()) +
        " remaining=" + std::to_string(session.pending_frames.size()) +
        " session_ready=" + std::string{from_session_ready ? "1" : "0"});
    Trace("FRAME_WRITE_BEGIN",
          ae::Format("id={} peer={} generation={} size={}", frame.id,
                     session.remote_uid, session.generation, frame.bytes.size()));
    if (on_pre_write_) {
      on_pre_write_(session.remote_uid, frame.bytes.size());
    }
    (void)session.stream->Write(std::move(frame.bytes));
    Trace("FRAME_WRITE_END",
          ae::Format("id={} peer={} generation={}", frame.id, session.remote_uid,
                     session.generation));
    if (frame.kind == "join_request") {
      Trace("JOIN_REQUEST_PHYSICAL_WRITE",
            ae::Format("id={} peer={} generation={}", frame.id,
                       session.remote_uid, session.generation));
    } else if (frame.kind == "join_accepted") {
      Trace("JOIN_ACCEPTED_PHYSICAL_WRITE",
            ae::Format("id={} peer={} generation={}", frame.id,
                       session.remote_uid, session.generation));
    }
    Log("NETWORK_FRAME_WRITTEN id=" + std::to_string(frame.id) +
        " peer=" + FormatAetherUid(session.remote_uid) +
        " generation=" + std::to_string(session.generation));
  }
}

void AetherP2pTransport::NotifySessionReadyWhenWritable(PeerSession& session) {
  if (session.stream == nullptr) {
    return;
  }
  auto const info = session.stream->stream_info();
  Log("P2P_STREAM_STATE peer=" + FormatAetherUid(session.remote_uid) +
      " generation=" + std::to_string(session.generation) +
      " link_state=" + std::string{LinkStateName(info.link_state)} +
      " writable=" + std::string{info.is_writable ? "1" : "0"});
  if (info.link_state == ae::LinkState::kLinked) {
    Trace("P2P_SESSION_LINKED",
          ae::Format("peer={} generation={} writable={}", session.remote_uid,
                     session.generation, info.is_writable ? 1 : 0));
  }
  if (info.link_state != ae::LinkState::kLinked || !info.is_writable) {
    Log("P2P_SESSION_LINK_WAIT peer=" + FormatAetherUid(session.remote_uid) +
        " generation=" + std::to_string(session.generation) +
        " link_state=" + std::string{LinkStateName(info.link_state)} +
        " writable=" + std::string{info.is_writable ? "1" : "0"});
    return;
  }

  if (!session.session_ready_flushed) {
    // Primary flush once per SessionReady.
    FlushPendingFrames(session, true);
    session.session_ready_flushed = true;
  } else if (!session.pending_frames.empty()) {
    FlushPendingFrames(session, false);
  }

  if (session.announced_ready) {
    return;
  }
  session.announced_ready = true;
  Trace("P2P_SESSION_WRITABLE",
        ae::Format("peer={} generation={}", session.remote_uid,
                   session.generation));
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
    ae::Uid const& peer, ae::P2pPortHandle handle, char const* source,
    std::deque<PendingFrame> preserved_pending) {
  if (!aether_app_ || !local_client_) {
    return nullptr;
  }

  auto const key = UidKey(peer);
  auto& next_gen = next_generation_[key];
  ++next_gen;

  Log("P2P_SESSION_CREATE_BEGIN peer=" + FormatAetherUid(peer) +
      " generation=" + std::to_string(next_gen) +
      " source=" + std::string{source != nullptr ? source : "unknown"} +
      " preserved_pending=" + std::to_string(preserved_pending.size()));

  auto session = std::make_unique<PeerSession>();
  session->remote_uid = peer;
  session->generation = next_gen;
  session->source = source != nullptr ? source : "unknown";
  session->pending_frames = std::move(preserved_pending);
  session->stream = std::make_shared<ae::P2pStream>(
      *aether_app_, local_client_.Load(), peer, std::move(handle));

  auto const generation = session->generation;
  session->data_sub = session->stream->out_data_event().Subscribe(
      [this, peer, generation](ae::DataBuffer const& data) {
        OnRawStreamData(peer, generation, data);
      });

  Trace("P2P_SESSION_CREATE",
        ae::Format("peer={} generation={} source={}", peer, generation,
                   source != nullptr ? source : "unknown"));
  Log("P2P_SESSION_CREATE peer=" + FormatAetherUid(peer) +
      " generation=" + std::to_string(generation) +
      " source=" + std::string{source != nullptr ? source : "unknown"});

  sessions_.push_back(std::move(session));
  auto* raw = sessions_.back().get();
  Trace("P2P_LIVE_SESSION_COUNT",
        ae::Format("peer={} count={}", peer, live_session_count(peer)));
  // Capture peer + generation only — never raw PeerSession* (UAF on replace).
  raw->link_sub = raw->stream->stream_update_event().Subscribe(
      [this, peer, generation]() {
        AssertNetworkThread("AetherP2pTransport::link_callback");
        auto* live = FindSession(peer, generation);
        if (live == nullptr) {
          Log("P2P_LINK_CALLBACK_STALE peer=" + FormatAetherUid(peer) +
              " generation=" + std::to_string(generation));
          return;
        }
        NotifySessionReadyWhenWritable(*live);
      });
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
  AssertNetworkThread("AetherP2pTransport::DropSession");
  for (auto it = sessions_.begin(); it != sessions_.end();) {
    if (*it == nullptr || (*it)->remote_uid != peer) {
      ++it;
      continue;
    }
    Trace("P2P_SESSION_DROP",
          ae::Format("peer={} generation={}", peer, (*it)->generation));
    DestroySessionLocked(**it, "drop", true);
    it = sessions_.erase(it);
  }
}

void AetherP2pTransport::Connect(ae::Uid const& remote_uid) {
  AssertNetworkThread("AetherP2pTransport::Connect");
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
  std::deque<PendingFrame> preserved;
  if (auto* existing = FindSession(remote_uid)) {
    preserved = TakePendingFrames(*existing);
  }
  DropSession(remote_uid);
  auto handle =
      local_client_->message_stream_manager().CreatePort(remote_uid);
  (void)CreateSession(remote_uid, std::move(handle), "reconnect",
                      std::move(preserved));
  reconnect_in_flight_.erase(key);
}

void AetherP2pTransport::AttachIncoming(ae::P2pPortHandle handle) {
  AssertNetworkThread("AetherP2pTransport::AttachIncoming");
  auto const peer = handle.destination();
  auto const old_generation = session_generation(peer);
  Trace("P2P_SESSION_INCOMING",
        ae::Format("peer={} generation={}", peer, old_generation));
  Log("P2P_ATTACH_INCOMING_BEGIN peer=" + FormatAetherUid(peer) +
      " generation=" + std::to_string(old_generation));
  std::deque<PendingFrame> preserved;
  if (FindSession(peer) != nullptr) {
    Trace("REPLACE_BEGIN",
          ae::Format("peer={} old={}", peer, old_generation));
    Log("P2P_SESSION_REPLACE_BEGIN peer=" + FormatAetherUid(peer) +
        " source=incoming");
    if (auto* existing = FindSession(peer)) {
      preserved = TakePendingFrames(*existing);
      Log("P2P_SESSION_REPLACE_PRESERVE peer=" + FormatAetherUid(peer) +
          " pending=" + std::to_string(preserved.size()));
      // Unsubscribe without clearing preserved pending (already taken).
      DestroySessionLocked(*existing, "replace_incoming", false);
      for (auto it = sessions_.begin(); it != sessions_.end(); ++it) {
        if (it->get() == existing) {
          sessions_.erase(it);
          break;
        }
      }
    }
    Log("P2P_SESSION_REPLACE_END peer=" + FormatAetherUid(peer) +
        " source=incoming");
  }
  (void)CreateSession(peer, std::move(handle), "incoming",
                      std::move(preserved));
  auto const new_generation = session_generation(peer);
  if (old_generation != 0) {
    Trace("REPLACE_END",
          ae::Format("peer={} old={} new={}", peer, old_generation,
                     new_generation));
  }
  Log("P2P_ATTACH_INCOMING_END peer=" + FormatAetherUid(peer) +
      " generation=" + std::to_string(new_generation));
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
  AssertNetworkThread("AetherP2pTransport::Send");
  auto* session = GetOrCreateSession(remote_uid);
  if (session == nullptr || session->stream == nullptr) {
    return;
  }
  auto const kind = ClassifyPayload(bytes, size);
  auto frame_bytes = EncodeAetherP2pFrame(bytes, size);
  auto const frame_id = AllocFrameId();
  Trace("FRAME_CREATED",
        ae::Format("id={} peer={} generation={} kind={} size={}", frame_id,
                   remote_uid, session->generation, kind, size));
  if (!session->announced_ready || !StreamWritable(*session)) {
    PendingFrame pending;
    pending.id = frame_id;
    pending.bytes = std::move(frame_bytes);
    pending.kind = kind;
    session->pending_frames.push_back(std::move(pending));
    Trace("FRAME_FIFO_QUEUED",
          ae::Format("id={} peer={} generation={} kind={}", frame_id, remote_uid,
                     session->generation, kind));
    if (kind == "join_request") {
      Trace("JOIN_REQUEST_FIFO_QUEUED",
            ae::Format("id={} peer={} generation={}", frame_id, remote_uid,
                       session->generation));
    } else if (kind == "join_accepted") {
      Trace("JOIN_ACCEPTED_FIFO_QUEUED",
            ae::Format("id={} peer={} generation={}", frame_id, remote_uid,
                       session->generation));
    }
    Log("NETWORK_FRAME_QUEUED id=" + std::to_string(frame_id) +
        " peer=" + FormatAetherUid(remote_uid) +
        " generation=" + std::to_string(session->generation) +
        " pending=" + std::to_string(session->pending_frames.size()));
    NotifySessionReadyWhenWritable(*session);
    return;
  }
  if (!flushed_frame_ids_.insert(frame_id).second) {
    Trace("FRAME_DUPLICATE_WRITE",
          ae::Format("id={} peer={} generation={}", frame_id, remote_uid,
                     session->generation));
    Log("NETWORK_FRAME_DUPLICATE_SUPPRESSED id=" + std::to_string(frame_id) +
        " peer=" + FormatAetherUid(remote_uid));
    return;
  }
  Trace("FRAME_WRITE_BEGIN",
        ae::Format("id={} peer={} generation={} size={}", frame_id, remote_uid,
                   session->generation, frame_bytes.size()));
  if (on_pre_write_) {
    on_pre_write_(remote_uid, frame_bytes.size());
  }
  (void)session->stream->Write(std::move(frame_bytes));
  Trace("FRAME_WRITE_END",
        ae::Format("id={} peer={} generation={}", frame_id, remote_uid,
                   session->generation));
  if (kind == "join_request") {
    Trace("JOIN_REQUEST_PHYSICAL_WRITE",
          ae::Format("id={} peer={} generation={}", frame_id, remote_uid,
                     session->generation));
  } else if (kind == "join_accepted") {
    Trace("JOIN_ACCEPTED_PHYSICAL_WRITE",
          ae::Format("id={} peer={} generation={}", frame_id, remote_uid,
                     session->generation));
  }
  Log("NETWORK_FRAME_WRITTEN id=" + std::to_string(frame_id) +
      " peer=" + FormatAetherUid(remote_uid) +
      " generation=" + std::to_string(session->generation) +
      " bytes=" + std::to_string(size));
}

void AetherP2pTransport::OnRawStreamData(ae::Uid peer, std::uint64_t generation,
                                         ae::DataBuffer const& data) {
  Log("P2P_STREAM_DATA peer=" + FormatAetherUid(peer) +
      " generation=" + std::to_string(generation) +
      " bytes=" + std::to_string(data.size()));
  auto* session = FindSession(peer, generation);
  if (session == nullptr) {
    Log("P2P_SESSION_CALLBACK_STALE_DROPPED peer=" + FormatAetherUid(peer) +
        " generation=" + std::to_string(generation));
    return;
  }
  session->decoder.Append(data.data(), data.size());
  session->decoder.Drain(
      [this, peer, generation](std::vector<std::uint8_t> const& payload) {
        auto* live = FindSession(peer, generation);
        if (live == nullptr) {
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
  auto const kind = ClassifyPayload(payload);
  if (kind == "join_request") {
    Trace("JOIN_REQUEST_NETWORK_RECEIVED",
          ae::Format("peer={} size={}", peer, payload.size()));
  } else if (kind == "join_accepted") {
    Trace("JOIN_ACCEPTED_NETWORK_RECEIVED",
          ae::Format("peer={} size={}", peer, payload.size()));
  }
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
