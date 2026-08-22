#include "chat_sync_controller.h"

#include <algorithm>
#include <cassert>
#include <utility>

#include "aether-miscpp/format/format.h"

#include "chat_presence.h"
#include "model/chat_peer_set.h"
#include "model/chat_presenter.h"
namespace apptraverse::chat {
namespace {

std::string FormatUid(ae::Uid const& uid) { return ae::Format("{}", uid); }

}  // namespace

ChatSyncController::ChatSyncController(SyncReplica replica, Chat::ptr chat,
                                       ChatPeerSet::ptr peer_set,
                                       SendFunction send,
                                       RawSendFunction raw_send,
                                       ChatSyncTiming timing,
                                       ChangedFunction changed,
                                       LogFunction log)
    : replica_{replica},
      chat_{std::move(chat)},
      peer_set_{std::move(peer_set)},
      send_{std::move(send)},
      raw_send_{std::move(raw_send)},
      timing_{timing},
      changed_{std::move(changed)},
      log_{std::move(log)} {
  assert(send_);
  assert(raw_send_);
  assert(chat_.is_valid());
  assert(peer_set_.is_valid());
  assert(replica_.shared_root_id == chat_.id());
}

void ChatSyncController::Log(std::string const& line) {
  if (log_) {
    log_(line);
  }
}

void ChatSyncController::NotifyChanged() {
  if (changed_) {
    changed_();
  }
}

SharedGraphSyncSession* ChatSyncController::FindSession(
    ae::Uid const& remote_uid) {
  if (auto* runtime = FindRuntime(remote_uid)) {
    return runtime->session.get();
  }
  return nullptr;
}

SharedGraphSyncSession const* ChatSyncController::FindSession(
    ae::Uid const& remote_uid) const {
  if (auto const* runtime = FindRuntime(remote_uid)) {
    return runtime->session.get();
  }
  return nullptr;
}
bool ChatSyncController::IsPeerOnline(ae::Uid const& remote_uid) const {
  if (auto const* runtime = FindRuntime(remote_uid)) {
    return runtime->currently_online;
  }
  return false;
}

PeerReachability ChatSyncController::GetPeerReachability(
    ae::Uid const& remote_uid) const {
  if (auto const* runtime = FindRuntime(remote_uid)) {
    return runtime->reachability;
  }
  return PeerReachability::kUnknown;
}

bool ChatSyncController::IsPeerOfflineMissedVisit(
    ae::Uid const& remote_uid) const {
  return GetPeerReachability(remote_uid) ==
         PeerReachability::kOfflineMissedPing;
}

bool ChatSyncController::IsPeerOfflineNoFuturePing(
    ae::Uid const& remote_uid) const {
  return GetPeerReachability(remote_uid) ==
         PeerReachability::kOfflineNoFuturePing;
}

bool ChatSyncController::ShowOfflinePingMarker(
    ae::Uid const& remote_uid) const {
  return apptraverse::chat::ShowOfflinePingMarker(
      GetPeerReachability(remote_uid));
}

void ChatSyncController::SetQueryPeerSchedule(QueryPeerScheduleFunction fn) {
  query_peer_schedule_ = std::move(fn);
}

std::size_t ChatSyncController::write_gate_size(
    ae::Uid const& remote_uid) const {
  if (auto const* runtime = FindRuntime(remote_uid)) {
    return runtime->write_gate.size();
  }
  return 0;
}

std::uint64_t ChatSyncController::physical_attempt_count(
    ae::Uid const& remote_uid, ae::ObjId packet_id) const {
  if (auto const* runtime = FindRuntime(remote_uid)) {
    return runtime->write_gate.attempt_count(packet_id);
  }
  return 0;
}

bool ChatSyncController::write_gate_has(ae::Uid const& remote_uid,
                                        ae::ObjId packet_id) const {
  if (auto const* runtime = FindRuntime(remote_uid)) {
    return runtime->write_gate.Has(packet_id);
  }
  return false;
}

ChatSyncController::RuntimeSession* ChatSyncController::FindRuntime(
    ae::Uid const& remote_uid) {
  for (auto& runtime : sessions_) {
    if (runtime.remote_uid == remote_uid) {
      return &runtime;
    }
  }
  return nullptr;
}

ChatSyncController::RuntimeSession const* ChatSyncController::FindRuntime(
    ae::Uid const& remote_uid) const {
  for (auto const& runtime : sessions_) {
    if (runtime.remote_uid == remote_uid) {
      return &runtime;
    }
  }
  return nullptr;
}

void ChatSyncController::EmitInitialMarkers(RuntimeSession& runtime) {
  assert(runtime.session != nullptr);
  if (runtime.session->initial_sync_complete()) {
    runtime.last_initial_sync_complete = true;
    Log(ae::Format("CHAT_SYNC_RESUMED peer={} initial_complete=1 pending={}",
                   FormatUid(runtime.remote_uid),
                   runtime.session->pending_packet_count()));
  }
}

void ChatSyncController::SendPresence(RuntimeSession& runtime,
                                      ChatPresenceMessage message,
                                      ae::TimePoint now) {
  assert(raw_send_);
  raw_send_(runtime.remote_uid, EncodeChatPresence(message));
  if (message == ChatPresenceMessage::kOnline ||
      message == ChatPresenceMessage::kHeartbeat) {
    runtime.last_heartbeat_sent = now;
  }
}

void ChatSyncController::ApplyOnlineTransition(RuntimeSession& runtime) {
  if (!runtime.ever_seen_online) {
    runtime.ever_seen_online = true;
    runtime.currently_online = true;
    Log(ae::Format("CHAT_PEER_ONLINE peer={}", FormatUid(runtime.remote_uid)));
    RequestPeerSchedule(runtime);
    NotifyChanged();
    return;
  }
  if (!runtime.currently_online) {
    runtime.currently_online = true;
    Log(ae::Format("CHAT_PEER_REJOINED peer={}",
                   FormatUid(runtime.remote_uid)));
    // SessionReady may have flushed while the remote process was still
    // binding; allow exactly one additional flush on offline→online.
    if (!ConfirmedOfflineHold(runtime.reachability)) {
      FlushPendingOnPresenceRejoin(runtime);
    }
    NotifyChanged();
  }
}

void ChatSyncController::ApplyOfflineTransition(RuntimeSession& runtime,
                                                char const* reason) {
  if (!runtime.currently_online) {
    return;
  }
  runtime.currently_online = false;
  runtime.recovery_flush_done = false;
  runtime.stale_path_reconnect_requested = false;
  runtime.rebuild_after_offline = true;
  Log(ae::Format("CHAT_PEER_OFFLINE peer={} reason={}",
                 FormatUid(runtime.remote_uid), reason));
  // Aether has no "did the other UID miss its server ping?" query. The local
  // equivalent is this presence miss: hold unconfirmed packets until the
  // returning side announces on session start.
  if (runtime.session != nullptr &&
      runtime.session->pending_packet_count() > 0) {
    Log(ae::Format(
        "SYNC_RETRY_HELD peer={} reason=peer_missed_presence pending_count={}",
        FormatUid(runtime.remote_uid), runtime.session->pending_packet_count()));
  }
  NotifyChanged();
}

bool ChatSyncController::TryRecoveryFlush(RuntimeSession& runtime,
                                          char const* reason,
                                          std::uint64_t transport_generation) {
  assert(runtime.session != nullptr);
  auto const pending = runtime.session->pending_packet_count();
  char const* const reason_text = reason != nullptr ? reason : "transport";
  if (!PayloadRetriesAllowed(runtime)) {
    Log(ae::Format(
        "SYNC_RETRY_HELD_SCHEDULE peer={} reason={} pending_count={}",
        FormatUid(runtime.remote_uid), reason_text, pending));
    return false;
  }
  if (pending == 0) {
    runtime.recovery_flush_done = false;
    return false;
  }
  if (runtime.recovery_flush_done) {
    Log(ae::Format(
        "SYNC_RECONNECT_FLUSH_SUPPRESSED peer={} generation={} "
        "reason=already_flushed_recovery requested={}",
        FormatUid(runtime.remote_uid), transport_generation, reason_text));
    return false;
  }
  runtime.recovery_flush_done = true;
  Log(ae::Format(
      "CHAT_SYNC_RECONNECT_BEGIN peer={} generation={} pending_count={} "
      "reason={}",
      FormatUid(runtime.remote_uid), transport_generation, pending,
      reason_text));
  Log(ae::Format(
      "SYNC_RECONNECT_FLUSH peer={} generation={} pending_count={} reason={}",
      FormatUid(runtime.remote_uid), transport_generation, pending,
      reason_text));
  Log(ae::Format(
      "CHAT_PENDING_FLUSH_BEGIN peer={} generation={} pending_count={} "
      "reason={}",
      FormatUid(runtime.remote_uid), transport_generation, pending,
      reason_text));
  // Reset gate so the first physical attempt after recovery is immediate;
  // subsequent retries stay under SyncPacketWriteGate (2000 ms).
  runtime.write_gate.Clear();
  auto const now = last_tick_now_.has_value() ? *last_tick_now_ : ae::Now();
  runtime.last_retry = now;
  // Re-offer here rather than via DrivePending: this is the sanctioned write
  // moment, so it must not be filtered by the retry cadence.
  runtime.session->RetryPending();
  Log(ae::Format(
      "CHAT_SYNC_RECONNECT_END peer={} generation={} pending_count={} "
      "reason={}",
      FormatUid(runtime.remote_uid), transport_generation,
      runtime.session->pending_packet_count(), reason_text));
  return true;
}

void ChatSyncController::FlushPendingImmediate(
    RuntimeSession& runtime, std::uint64_t transport_generation,
    char const* reason) {
  assert(runtime.session != nullptr);
  if (transport_generation == 0) {
    Log(ae::Format(
        "SYNC_RECONNECT_FLUSH_SUPPRESSED peer={} reason=invalid_generation",
        FormatUid(runtime.remote_uid)));
    return;
  }
  if (transport_generation <= runtime.last_flushed_transport_generation) {
    Log(ae::Format(
        "SYNC_RECONNECT_FLUSH_SUPPRESSED peer={} generation={} "
        "reason=already_flushed_generation",
        FormatUid(runtime.remote_uid), transport_generation));
    return;
  }
  runtime.last_flushed_transport_generation = transport_generation;
  // A brand new transport session is fresh evidence of reachability: it may
  // re-arm the shared recovery token spent while the old session was dead.
  runtime.recovery_flush_done = false;
  // This generation is the outbound path rebuild we may have asked for.
  runtime.stale_path_reconnect_requested = false;
  runtime.rebuild_after_offline = false;
  TryRecoveryFlush(runtime, reason, transport_generation);
}

bool ChatSyncController::RequestStalePathReconnect(RuntimeSession& runtime,
                                                   char const* reason) {
  assert(runtime.session != nullptr);
  if (!runtime.rebuild_after_offline || !reconnect_peer_ ||
      runtime.session->pending_packet_count() == 0 ||
      runtime.stale_path_reconnect_requested) {
    return false;
  }
  // Inbound frames prove the remote is up, but our outbound session was built
  // against its previous process. Re-offering pending packets on that path only
  // buys another wait for the cloud to notice; a fresh session routes at once.
  runtime.stale_path_reconnect_requested = true;
  Log(ae::Format("SYNC_STALE_PATH_RECONNECT peer={} pending_count={} reason={}",
                 FormatUid(runtime.remote_uid),
                 runtime.session->pending_packet_count(),
                 reason != nullptr ? reason : "unknown"));
  reconnect_peer_(runtime.remote_uid);
  return true;
}

void ChatSyncController::FlushPendingOnPresenceRejoin(RuntimeSession& runtime) {
  if (RequestStalePathReconnect(runtime, "presence_rejoin")) {
    // The new transport generation drives the flush; nothing to gain from
    // writing to the path we just replaced.
    return;
  }
  TryRecoveryFlush(runtime, "presence_rejoin",
                   runtime.last_flushed_transport_generation);
}

void ChatSyncController::FlushPendingOnPeerActivity(RuntimeSession& runtime) {
  assert(runtime.session != nullptr);
  if (runtime.session->pending_packet_count() == 0) {
    runtime.recovery_flush_done = false;
    runtime.stale_path_reconnect_requested = false;
    runtime.rebuild_after_offline = false;
    return;
  }
  // Checked before the recovery token: a spent token means we already re-offered
  // packets on the old path, which is exactly the case a rebuild has to escape.
  if (RequestStalePathReconnect(runtime, "peer_activity")) {
    return;
  }
  if (runtime.recovery_flush_done) {
    // Steady-state inbound on a live path: retry cadence owns the writes.
    return;
  }
  // Remote is delivering on the wire again after silence: this is the first
  // moment a pending write can actually reach it.
  TryRecoveryFlush(runtime, "peer_activity",
                   runtime.last_flushed_transport_generation);
}

void ChatSyncController::NotifyTransportSessionReady(
    ae::Uid const& remote_uid, std::uint64_t transport_generation) {
  auto* runtime = FindRuntime(remote_uid);
  if (runtime == nullptr || runtime->session == nullptr) {
    return;
  }
  Log(ae::Format("CHAT_TRANSPORT_SESSION_READY peer={} generation={}",
                 FormatUid(remote_uid), transport_generation));
  // Returning side announces immediately so the waiting side does not have to
  // keep offering pending packets into a dead outbound path.
  Log(ae::Format("CHAT_STARTUP_NOTIFY peer={} generation={}",
                 FormatUid(remote_uid), transport_generation));
  SendPresence(*runtime, ChatPresenceMessage::kOnline, ae::Now());
  // Session-ready / generation change must not lift an OfflineMissedVisit hold.
  if (PayloadRetriesAllowed(*runtime)) {
    FlushPendingImmediate(*runtime, transport_generation,
                          "transport_generation");
  } else {
    Log(ae::Format(
        "SYNC_PAYLOAD_RETRY_HELD peer={} reason=session_ready pending_count={}",
        FormatUid(remote_uid), runtime->session->pending_packet_count()));
  }
}

void ChatSyncController::DrivePresence(RuntimeSession& runtime,
                                       ae::TimePoint now) {
  if (runtime.currently_online && runtime.last_seen.has_value() &&
      now - *runtime.last_seen >= timing_.offline_timeout) {
    ApplyOfflineTransition(runtime, "timeout");
  }

  if (runtime.ever_seen_online && !runtime.currently_online) {
    // Peer missed its presence window: do not probe a path that cannot ACK.
    return;
  }

  bool const heartbeat_due =
      runtime.last_heartbeat_sent.time_since_epoch().count() == 0 ||
      now - runtime.last_heartbeat_sent >= timing_.heartbeat_interval;
  if (heartbeat_due) {
    SendPresence(runtime, ChatPresenceMessage::kHeartbeat, now);
  }
}

void ChatSyncController::DrivePending(RuntimeSession& runtime,
                                      ae::TimePoint now) {
  assert(runtime.session != nullptr);
  auto const pending = runtime.session->pending_packet_count();

  if (pending == 0) {
    if (runtime.last_pending_count > 0) {
      Log(ae::Format("CHAT_PENDING_CHANGED peer={} pending=0",
                     FormatUid(runtime.remote_uid)));
      NotifyChanged();
    }
    if (runtime.offline_marker_on) {
      ClearOfflinePingMarker(runtime);
    }
    runtime.last_pending_count = 0;
    runtime.write_gate.Clear();
    runtime.recovery_flush_done = false;
    runtime.stale_path_reconnect_requested = false;
    runtime.rebuild_after_offline = false;
    return;
  }

  if (runtime.last_pending_count == 0) {
    Log(ae::Format("CHAT_PENDING_CHANGED peer={} pending={}",
                   FormatUid(runtime.remote_uid), pending));
    NotifyChanged();
  } else if (runtime.last_pending_count != pending) {
    Log(ae::Format("CHAT_PENDING_CHANGED peer={} pending={}",
                   FormatUid(runtime.remote_uid), pending));
    NotifyChanged();
  }
  runtime.last_pending_count = pending;
  PruneWriteGate(runtime);

  if (!PayloadRetriesAllowed(runtime)) {
    Log(ae::Format("SYNC_PAYLOAD_RETRY_HELD peer={}",
                   FormatUid(runtime.remote_uid)));
    MaybeRetryScheduleQuery(runtime, now);
    return;
  }

  bool const retry_due = runtime.last_retry.time_since_epoch().count() == 0 ||
                         now - runtime.last_retry >= timing_.retry_interval;
  if (retry_due) {
    runtime.session->RetryPending();
    runtime.last_retry = now;
  }
}

void ChatSyncController::PruneWriteGate(RuntimeSession& runtime) {
  assert(runtime.session != nullptr);
  std::vector<ae::ObjId> keep;
  keep.reserve(runtime.session->pending_packet_count());
  for (auto const& pending :
       runtime.session->state()->data.pending_packets) {
    keep.push_back(pending.packet_id);
  }
  runtime.write_gate.RetainOnly(keep);
}

bool ChatSyncController::IsPersistentPending(RuntimeSession const& runtime,
                                             ae::ObjId packet_id) const {
  assert(runtime.session != nullptr);
  for (auto const& pending :
       runtime.session->state()->data.pending_packets) {
    if (pending.packet_id == packet_id) {
      return true;
    }
  }
  return false;
}

void ChatSyncController::OfferPhysicalSend(RuntimeSession& runtime,
                                           ae::ObjId packet_id,
                                           SerializedSyncPacket bytes,
                                           ae::TimePoint now) {
  // One-shot packets (application ACKs) are not in persistent pending —
  // send once with no gate slot.
  if (!IsPersistentPending(runtime, packet_id)) {
    Log(ae::Format("SYNC_TRANSPORT_WRITE peer={} packet={} oneshot=1",
                   FormatUid(runtime.remote_uid), packet_id.id()));
    Log(ae::Format("SYNC_TRANSPORT_SEND peer={} packet={}",
                   FormatUid(runtime.remote_uid), packet_id.id()));
    send_(runtime.remote_uid, packet_id, bytes);
    return;
  }

  if (!PayloadRetriesAllowed(runtime)) {
    Log(ae::Format("SYNC_PAYLOAD_RETRY_HELD peer={} packet={}",
                   FormatUid(runtime.remote_uid), packet_id.id()));
    return;
  }

  if (!runtime.write_gate.TryBegin(packet_id, now)) {
    Log(ae::Format(
        "SYNC_WRITE_SUPPRESSED peer={} packet={} attempts={}",
        FormatUid(runtime.remote_uid), packet_id.id(),
        runtime.write_gate.attempt_count(packet_id)));
    return;
  }

  Log(ae::Format("CHAT_SYNC_PAYLOAD_WRITE peer={} packet={} attempt={}",
                 FormatUid(runtime.remote_uid), packet_id.id(),
                 runtime.write_gate.attempt_count(packet_id)));
  Log(ae::Format("SYNC_TRANSPORT_WRITE peer={} packet={} attempt={}",
                 FormatUid(runtime.remote_uid), packet_id.id(),
                 runtime.write_gate.attempt_count(packet_id)));
  Log(ae::Format("SYNC_TRANSPORT_SEND peer={} packet={}",
                 FormatUid(runtime.remote_uid), packet_id.id()));
  send_(runtime.remote_uid, packet_id, bytes);
}

ChatSyncController::RuntimeSession& ChatSyncController::EnsureRuntimeSession(
    ae::Uid const& remote_uid, SyncSessionState::ptr state) {
  if (auto* existing = FindRuntime(remote_uid)) {
    return *existing;
  }

  assert(state.is_valid());
  state.Load();
  assert(state.is_loaded());

  RuntimeSession runtime;
  runtime.remote_uid = remote_uid;
  runtime.write_gate = SyncPacketWriteGate{timing_.packet_retry_interval};
  runtime.session = std::make_unique<SharedGraphSyncSession>(
      replica_, state,
      [this, remote_uid](ae::ObjId packet_id, SerializedSyncPacket bytes) {
        auto* runtime = FindRuntime(remote_uid);
        assert(runtime != nullptr);
        auto const now =
            last_tick_now_.has_value() ? *last_tick_now_ : ae::Now();
        OfferPhysicalSend(*runtime, packet_id, std::move(bytes), now);
      });
  if (log_) {
    runtime.session->set_trace(log_);
  }
  runtime.last_initial_sync_complete =
      runtime.session->initial_sync_complete();
  sessions_.push_back(std::move(runtime));
  return sessions_.back();
}

void ChatSyncController::Start() {
  peer_set_.Load();
  assert(peer_set_.is_loaded());
  auto const now = ae::Now();
  for (auto& peer : peer_set_->peers) {
    assert(!peer.remote_uid.empty());
    assert(peer.session_state.is_valid());
    auto& runtime =
        EnsureRuntimeSession(peer.remote_uid, peer.session_state);
    SendPresence(runtime, ChatPresenceMessage::kOnline, now);
    EmitInitialMarkers(runtime);
    runtime.session->StartOrResume();
    if (runtime.session->initial_sync_complete() &&
        !runtime.last_initial_sync_complete) {
      runtime.last_initial_sync_complete = true;
      Log(ae::Format("CHAT_SYNC_INITIAL_COMPLETE peer={}",
                     FormatUid(runtime.remote_uid)));
    }
  }
}

void ChatSyncController::Stop() {
  auto const now = ae::Now();
  for (auto& runtime : sessions_) {
    SendPresence(runtime, ChatPresenceMessage::kOffline, now);
    runtime.write_gate.Clear();
  }
}

void ChatSyncController::SetIncomingPeerAuthorize(
    IncomingPeerAuthorizeFunction fn) {
  incoming_peer_authorize_ = std::move(fn);
}

void ChatSyncController::SetReconnectPeer(ReconnectPeerFunction fn) {
  reconnect_peer_ = std::move(fn);
}

SharedGraphSyncSession& ChatSyncController::AddPeer(ae::Uid const& remote_uid) {
  assert(!remote_uid.empty());
  Log(ae::Format("CHAT_ADD_PEER_REQUEST peer={}", FormatUid(remote_uid)));
  if (auto* existing = FindSession(remote_uid)) {
    Log(ae::Format("CHAT_ADD_PEER_RESULT peer={} result=already_present",
                   FormatUid(remote_uid)));
    return *existing;
  }

  peer_set_.Load();
  assert(peer_set_.is_loaded());
  auto const& peer = AddChatPeer(peer_set_, chat_.id(), remote_uid);
  assert(peer.session_state.is_valid());
  Log(ae::Format("CHAT_PEER_ADDED uid={} session_state_id={}",
                 FormatUid(remote_uid), peer.session_state.id().id()));
  Log(ae::Format("SYNC_SESSION_CREATE peer={}", FormatUid(remote_uid)));

  auto& runtime =
      EnsureRuntimeSession(remote_uid, peer.session_state);
  SendPresence(runtime, ChatPresenceMessage::kOnline, ae::Now());
  EmitInitialMarkers(runtime);
  runtime.session->StartOrResume();
  Log(ae::Format("CHAT_ADD_PEER_RESULT peer={} result=added",
                 FormatUid(remote_uid)));
  if (runtime.session->initial_sync_complete() &&
      !runtime.last_initial_sync_complete) {
    runtime.last_initial_sync_complete = true;
    Log(ae::Format("CHAT_SYNC_INITIAL_COMPLETE peer={}",
                   FormatUid(runtime.remote_uid)));
    Log(ae::Format("SYNC_INITIAL_COMPLETE peer={}",
                   FormatUid(runtime.remote_uid)));
  }
  return *runtime.session;
}

void ChatSyncController::QueueAutoAccept(
    ae::Uid const& remote_uid, std::vector<std::uint8_t> const& bytes) {
  for (auto& pending : pending_auto_accept_) {
    if (pending.remote_uid == remote_uid) {
      pending.packets.push_back(bytes);
      return;
    }
  }
  PendingAutoAccept pending;
  pending.remote_uid = remote_uid;
  pending.packets.push_back(bytes);
  pending_auto_accept_.push_back(std::move(pending));
  Log(ae::Format("CHAT_PEER_AUTO_ACCEPT_QUEUED uid={}", FormatUid(remote_uid)));
}

void ChatSyncController::ReceiveKnown(ae::Uid const& remote_uid,
                                      std::vector<std::uint8_t> const& bytes) {
  auto* runtime = FindRuntime(remote_uid);
  assert(runtime != nullptr);

  auto const now = ae::Now();
  auto const presence = TryDecodeChatPresence(bytes);
  if (presence.has_value() &&
      *presence == ChatPresenceMessage::kOffline) {
    runtime->last_seen = now;
    ApplyOfflineTransition(*runtime, "explicit");
    return;
  }

  // Inbound traffic after a silence gap is the only hard evidence that the
  // remote is reachable again. Timer-based detectors (stale peer) may have
  // spent the recovery token while the remote was still absent, so re-arm it
  // here; otherwise the first pending re-offer waits out the write gate.
  bool const was_silent =
      !runtime->currently_online || !runtime->last_seen.has_value() ||
      now - *runtime->last_seen >= timing_.heartbeat_interval;
  if (was_silent) {
    runtime->recovery_flush_done = false;
  }
  runtime->last_seen = now;
  bool const explicit_online =
      presence.has_value() && *presence == ChatPresenceMessage::kOnline;
  if (explicit_online) {
    RearmFromPeerOnlineNotify(*runtime);
    return;
  }
  ApplyOnlineTransition(*runtime);

  if (presence.has_value()) {
    if (!ConfirmedOfflineHold(runtime->reachability)) {
      FlushPendingOnPeerActivity(*runtime);
    }
    return;
  }

  assert(runtime->session != nullptr);
  auto const pending_before = runtime->session->pending_packet_count();
  runtime->session->Receive(bytes);
  auto const pending_after = runtime->session->pending_packet_count();
  // Application ACK may have removed pending packets — drop gate slots now.
  if (pending_after == 0) {
    runtime->write_gate.Clear();
    runtime->last_pending_count = 0;
    runtime->recovery_flush_done = false;
  } else {
    PruneWriteGate(*runtime);
    runtime->last_pending_count = pending_after;
    if (pending_after < pending_before) {
      // Real progress: the next packet in the backlog earns an immediate slot.
      runtime->recovery_flush_done = false;
    }
    // After inbound sync on a live path, re-offer gated pending immediately.
    FlushPendingOnPeerActivity(*runtime);
  }

  if (!runtime->last_initial_sync_complete &&
      runtime->session->initial_sync_complete()) {
    runtime->last_initial_sync_complete = true;
    Log(ae::Format("CHAT_SYNC_INITIAL_COMPLETE peer={}",
                   FormatUid(runtime->remote_uid)));
  }

  NotifyChanged();
}

void ChatSyncController::DrainPendingAutoAccept() {
  std::vector<PendingAutoAccept> ready;
  for (auto& pending : pending_auto_accept_) {
    if (!pending.armed) {
      pending.armed = true;
      continue;
    }
    ready.push_back(std::move(pending));
    pending.remote_uid = ae::Uid{};
  }
  pending_auto_accept_.erase(
      std::remove_if(pending_auto_accept_.begin(), pending_auto_accept_.end(),
                     [](PendingAutoAccept const& pending) {
                       return pending.remote_uid.empty();
                     }),
      pending_auto_accept_.end());

  for (auto& pending : ready) {
    assert(!pending.remote_uid.empty());
    AddPeer(pending.remote_uid);
    for (auto const& packet : pending.packets) {
      ReceiveKnown(pending.remote_uid, packet);
    }
  }
}

void ChatSyncController::Receive(ae::Uid const& remote_uid,
                                 std::vector<std::uint8_t> const& bytes) {
  assert(!remote_uid.empty());
  Log(ae::Format("SYNC_TRANSPORT_RECEIVE peer={} bytes={}",
                 FormatUid(remote_uid), bytes.size()));
  if (FindRuntime(remote_uid) == nullptr) {
    if (incoming_peer_authorize_ && !incoming_peer_authorize_(remote_uid)) {
      Log(ae::Format("CHAT_PEER_UNAUTHORIZED_DROP uid={}", FormatUid(remote_uid)));
      Log(ae::Format("CHAT_SYNC_AUTH peer={} result=deny reason=unauthorized",
                     FormatUid(remote_uid)));
      return;
    }
    Log(ae::Format("CHAT_SYNC_AUTH peer={} result=allow reason=auto_accept",
                   FormatUid(remote_uid)));
    // Defer AddPeer/session creation out of the transport receive callback.
    QueueAutoAccept(remote_uid, bytes);
    return;
  }

  Log(ae::Format("CHAT_SYNC_AUTH peer={} result=allow reason=known_session",
                 FormatUid(remote_uid)));
  ReceiveKnown(remote_uid, bytes);
}

void ChatSyncController::LocalEventCommitted(Node::ptr node,
                                             EventRecord const& record) {
  assert(node.is_valid());
  assert(record.event.is_valid());
  last_tick_now_ = ae::Now();
  Log(ae::Format("CHAT_EVENT_COMMITTED event={} target={}",
                 record.event.id().id(), node.id().id()));
  for (auto& runtime : sessions_) {
    assert(runtime.session != nullptr);
    runtime.session->PublishCommittedEvent(node, record);
  }
}

bool ChatSyncController::PayloadRetriesAllowed(
    RuntimeSession const& runtime) const {
  return runtime.reachability != PeerReachability::kScheduleCheckPending &&
         !ConfirmedOfflineHold(runtime.reachability);
}

void ChatSyncController::RequestPeerSchedule(RuntimeSession& runtime) {
  if (!query_peer_schedule_ || runtime.schedule_query_in_flight) {
    return;
  }
  runtime.schedule_query_in_flight = true;
  runtime.last_schedule_query =
      last_tick_now_.has_value() ? *last_tick_now_ : ae::Now();
  Log(ae::Format("PEER_UAP_QUERY_BEGIN peer={}", FormatUid(runtime.remote_uid)));
  auto const uid = runtime.remote_uid;
  query_peer_schedule_(uid, [this, uid](std::optional<PeerScheduleSnapshot> result) {
    if (auto* rt = FindRuntime(uid)) {
      rt->schedule_query_in_flight = false;
      OnPeerScheduleResult(*rt, std::move(result));
    }
  });
}

void ChatSyncController::ApplyTickDeadline(RuntimeSession& runtime,
                                           PeerScheduleSnapshot const& snap,
                                           ae::TimePoint now) {
  runtime.local_deadline = snap.local_deadline;
  if (!snap.local_deadline.has_value()) {
    runtime.tick_deadline.reset();
    return;
  }
  auto remaining_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      *snap.local_deadline - std::chrono::steady_clock::now());
  if (remaining_ms.count() < 0) {
    remaining_ms = std::chrono::milliseconds{0};
  }
  runtime.tick_deadline = now + remaining_ms;
}

void ChatSyncController::ImmediatePayloadRetry(RuntimeSession& runtime,
                                               ae::TimePoint now) {
  if (runtime.session == nullptr ||
      runtime.session->pending_packet_count() == 0) {
    return;
  }
  runtime.write_gate.Clear();
  runtime.session->RetryPending();
  runtime.last_retry = now;
}

void ChatSyncController::OnPeerScheduleResult(
    RuntimeSession& runtime, std::optional<PeerScheduleSnapshot> result) {
  if (!result.has_value()) {
    Log(ae::Format(
        "PEER_UAP_QUERY_RESULT peer={} error=1 last_read_ms=0 delta_ms=0",
        FormatUid(runtime.remote_uid)));
    return;
  }

  auto const previous_last = runtime.last_ping_server_ms;
  auto const had_uap = runtime.had_valid_uap;
  auto const now = last_tick_now_.has_value() ? *last_tick_now_ : ae::Now();
  bool const advanced =
      had_uap && result->last_ping_server_ms > previous_last;

  runtime.last_ping_server_ms = result->last_ping_server_ms;
  runtime.next_ping_delta_ms = result->next_ping_delta_ms;
  runtime.had_valid_uap = true;
  ApplyTickDeadline(runtime, *result, now);

  Log(ae::Format(
      "PEER_UAP_QUERY_RESULT peer={} last_read_ms={} delta_ms={}",
      FormatUid(runtime.remote_uid), runtime.last_ping_server_ms,
      runtime.next_ping_delta_ms));

  if (ConfirmedOfflineHold(runtime.reachability) && !advanced) {
    return;
  }

  if (advanced) {
    Log(ae::Format("PEER_PING_ADVANCED peer={}",
                   FormatUid(runtime.remote_uid)));
    ImmediatePayloadRetry(runtime, now);
    if (runtime.next_ping_delta_ms > 0) {
      runtime.reachability = PeerReachability::kWaitingForScheduledPing;
      Log(ae::Format("SYNC_PAYLOAD_RETRY_ALLOWED peer={}",
                     FormatUid(runtime.remote_uid)));
    } else {
      EnterOfflineNoFuturePing(runtime);
    }
    NotifyChanged();
    return;
  }

  if (runtime.reachability == PeerReachability::kScheduleCheckPending) {
    if (runtime.next_ping_delta_ms <= 0) {
      EnterOfflineNoFuturePing(runtime);
      return;
    }
    EnterOfflineMissedPing(runtime);
    return;
  }

  if (runtime.next_ping_delta_ms > 0) {
    runtime.reachability = PeerReachability::kWaitingForScheduledPing;
    Log(ae::Format("SYNC_PAYLOAD_RETRY_ALLOWED peer={}",
                   FormatUid(runtime.remote_uid)));
  }
  NotifyChanged();
}

void ChatSyncController::MaybeHandleScheduleDeadline(RuntimeSession& runtime,
                                                     ae::TimePoint now) {
  if (runtime.reachability != PeerReachability::kWaitingForScheduledPing ||
      !runtime.tick_deadline.has_value() || now < *runtime.tick_deadline) {
    return;
  }
  Log(ae::Format("PEER_PING_DEADLINE peer={}", FormatUid(runtime.remote_uid)));
  runtime.reachability = PeerReachability::kScheduleCheckPending;
  Log(ae::Format("SYNC_PAYLOAD_RETRY_HELD peer={} reason=deadline",
                 FormatUid(runtime.remote_uid)));
  RequestPeerSchedule(runtime);
}

void ChatSyncController::MaybeRetryScheduleQuery(RuntimeSession& runtime,
                                                 ae::TimePoint now) {
  if (runtime.reachability != PeerReachability::kScheduleCheckPending ||
      runtime.schedule_query_in_flight) {
    return;
  }
  if (runtime.last_schedule_query.time_since_epoch().count() != 0 &&
      now - runtime.last_schedule_query < timing_.packet_retry_interval) {
    return;
  }
  RequestPeerSchedule(runtime);
}

void ChatSyncController::RearmFromPeerOnlineNotify(RuntimeSession& runtime) {
  bool const was_hold = ConfirmedOfflineHold(runtime.reachability);
  bool const first_online = !runtime.ever_seen_online;
  bool const was_offline = runtime.ever_seen_online && !runtime.currently_online;
  Log(ae::Format("PEER_ONLINE_NOTIFY peer={}", FormatUid(runtime.remote_uid)));
  runtime.ever_seen_online = true;
  runtime.currently_online = true;
  runtime.reachability = PeerReachability::kOnline;
  if (first_online) {
    Log(ae::Format("CHAT_PEER_ONLINE peer={}", FormatUid(runtime.remote_uid)));
  } else if (was_offline) {
    Log(ae::Format("CHAT_PEER_REJOINED peer={}",
                   FormatUid(runtime.remote_uid)));
  }
  if (was_hold) {
    ClearOfflinePingMarker(runtime);
  }
  Log(ae::Format("PEER_ONLINE_REARMED peer={}", FormatUid(runtime.remote_uid)));
  Log(ae::Format("SYNC_PAYLOAD_RETRY_ALLOWED peer={}",
                 FormatUid(runtime.remote_uid)));
  runtime.recovery_flush_done = false;
  ImmediatePayloadRetry(
      runtime, last_tick_now_.has_value() ? *last_tick_now_ : ae::Now());
  RequestPeerSchedule(runtime);
  NotifyChanged();
}

void ChatSyncController::EnterOfflineMissedPing(RuntimeSession& runtime) {
  runtime.reachability = PeerReachability::kOfflineMissedPing;
  Log(ae::Format("PEER_PING_MISSED peer={}", FormatUid(runtime.remote_uid)));
  if (!runtime.offline_marker_on && runtime.session != nullptr &&
      runtime.session->pending_packet_count() > 0) {
    runtime.offline_marker_on = true;
    Log(ae::Format("OFFLINE_PING_MARKER_ON peer={}",
                   FormatUid(runtime.remote_uid)));
  }
  NotifyChanged();
}

void ChatSyncController::EnterOfflineNoFuturePing(RuntimeSession& runtime) {
  runtime.reachability = PeerReachability::kOfflineNoFuturePing;
  Log(ae::Format("PEER_NO_FUTURE_PING peer={}", FormatUid(runtime.remote_uid)));
  if (!runtime.offline_marker_on && runtime.session != nullptr &&
      runtime.session->pending_packet_count() > 0) {
    runtime.offline_marker_on = true;
    Log(ae::Format("OFFLINE_PING_MARKER_ON peer={}",
                   FormatUid(runtime.remote_uid)));
  }
  NotifyChanged();
}

void ChatSyncController::ClearOfflinePingMarker(RuntimeSession& runtime) {
  if (runtime.offline_marker_on) {
    runtime.offline_marker_on = false;
    Log(ae::Format("OFFLINE_PING_MARKER_OFF peer={}",
                   FormatUid(runtime.remote_uid)));
  }
}

void ChatSyncController::Tick(ae::TimePoint now) {
  last_tick_now_ = now;
  DrainPendingAutoAccept();

  for (auto& runtime : sessions_) {
    assert(runtime.session != nullptr);
    runtime.session->Poll();
    MaybeHandleScheduleDeadline(runtime, now);
    DrivePending(runtime, now);
    DrivePresence(runtime, now);

    if (!runtime.last_initial_sync_complete &&
        runtime.session->initial_sync_complete()) {
      runtime.last_initial_sync_complete = true;
      Log(ae::Format("CHAT_SYNC_INITIAL_COMPLETE peer={}",
                     FormatUid(runtime.remote_uid)));
      NotifyChanged();
    }
  }
}

}  // namespace apptraverse::chat
