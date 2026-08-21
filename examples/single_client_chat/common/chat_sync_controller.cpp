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
    runtime.flushed_for_current_online_epoch = false;
    Log(ae::Format("CHAT_PEER_ONLINE peer={}", FormatUid(runtime.remote_uid)));
    NotifyChanged();
    return;
  }
  if (!runtime.currently_online) {
    runtime.currently_online = true;
    Log(ae::Format("CHAT_PEER_REJOINED peer={}",
                   FormatUid(runtime.remote_uid)));
    // SessionReady may have flushed while the remote process was still
    // binding; allow exactly one additional flush on offline→online.
    FlushPendingOnPresenceRejoin(runtime);
    NotifyChanged();
  }
}

void ChatSyncController::ApplyOfflineTransition(RuntimeSession& runtime,
                                                char const* reason) {
  if (!runtime.currently_online) {
    return;
  }
  runtime.currently_online = false;
  runtime.flushed_for_current_online_epoch = false;
  runtime.flushed_for_peer_activity_ = false;
  runtime.flushed_for_stale_peer_ = false;
  Log(ae::Format("CHAT_PEER_OFFLINE peer={} reason={}",
                 FormatUid(runtime.remote_uid), reason));
  NotifyChanged();
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
  auto const pending = runtime.session->pending_packet_count();
  runtime.last_flushed_transport_generation = transport_generation;
  Log(ae::Format(
      "CHAT_SYNC_RECONNECT_BEGIN peer={} generation={} pending_count={} "
      "reason={}",
      FormatUid(runtime.remote_uid), transport_generation, pending,
      reason != nullptr ? reason : "transport"));
  Log(ae::Format(
      "SYNC_RECONNECT_FLUSH peer={} generation={} pending_count={} reason={}",
      FormatUid(runtime.remote_uid), transport_generation, pending,
      reason != nullptr ? reason : "transport"));
  Log(ae::Format(
      "CHAT_PENDING_FLUSH_BEGIN peer={} generation={} pending_count={}",
      FormatUid(runtime.remote_uid), transport_generation, pending));
  // Reset gate so the first physical attempt after reconnect is immediate;
  // subsequent retries stay under SyncPacketWriteGate (2000 ms).
  runtime.write_gate.Clear();
  runtime.last_retry = {};
  auto const now = last_tick_now_.has_value() ? *last_tick_now_ : ae::Now();
  // DrivePending (not only RetryPending) so reconnect does not wait for Tick.
  DrivePending(runtime, now);
  // If pending remains (remote not yet ready to ACK), allow a later
  // peer-activity flush to clear a write-gate cooldown once inbound resumes.
  runtime.flushed_for_peer_activity_ =
      runtime.session->pending_packet_count() == 0;
  Log(ae::Format(
      "CHAT_SYNC_RECONNECT_END peer={} generation={} pending_count={}",
      FormatUid(runtime.remote_uid), transport_generation,
      runtime.session->pending_packet_count()));
}

void ChatSyncController::FlushPendingOnPresenceRejoin(RuntimeSession& runtime) {
  assert(runtime.session != nullptr);
  if (runtime.flushed_for_current_online_epoch) {
    Log(ae::Format(
        "SYNC_RECONNECT_FLUSH_SUPPRESSED peer={} "
        "reason=already_flushed_presence_epoch",
        FormatUid(runtime.remote_uid)));
    return;
  }
  runtime.flushed_for_current_online_epoch = true;
  auto const pending = runtime.session->pending_packet_count();
  Log(ae::Format(
      "CHAT_SYNC_RECONNECT_BEGIN peer={} generation={} pending_count={} "
      "reason=presence_rejoin",
      FormatUid(runtime.remote_uid),
      runtime.last_flushed_transport_generation, pending));
  Log(ae::Format(
      "SYNC_RECONNECT_FLUSH peer={} generation={} pending_count={} "
      "reason=presence_rejoin",
      FormatUid(runtime.remote_uid),
      runtime.last_flushed_transport_generation, pending));
  Log(ae::Format(
      "CHAT_PENDING_FLUSH_BEGIN peer={} generation={} pending_count={} "
      "reason=presence_rejoin",
      FormatUid(runtime.remote_uid),
      runtime.last_flushed_transport_generation, pending));
  if (pending == 0) {
    Log(ae::Format(
        "CHAT_SYNC_RECONNECT_END peer={} generation={} pending_count=0",
        FormatUid(runtime.remote_uid),
        runtime.last_flushed_transport_generation));
    return;
  }
  runtime.write_gate.Clear();
  runtime.last_retry = {};
  auto const now = last_tick_now_.has_value() ? *last_tick_now_ : ae::Now();
  DrivePending(runtime, now);
  runtime.flushed_for_peer_activity_ =
      runtime.session->pending_packet_count() == 0;
  Log(ae::Format(
      "CHAT_SYNC_RECONNECT_END peer={} generation={} pending_count={}",
      FormatUid(runtime.remote_uid),
      runtime.last_flushed_transport_generation,
      runtime.session->pending_packet_count()));
}

void ChatSyncController::FlushPendingOnPeerActivity(RuntimeSession& runtime) {
  assert(runtime.session != nullptr);
  auto const pending = runtime.session->pending_packet_count();
  if (pending == 0) {
    runtime.flushed_for_peer_activity_ = false;
    return;
  }
  if (runtime.flushed_for_peer_activity_) {
    return;
  }
  if (runtime.write_gate.size() == 0) {
    return;
  }
  // Remote is delivering on the wire again while our write-gate still holds
  // a cooldown from a pre-restart attempt. Clear once and DrivePending now.
  runtime.flushed_for_peer_activity_ = true;
  Log(ae::Format(
      "CHAT_SYNC_RECONNECT_BEGIN peer={} generation={} pending_count={} "
      "reason=peer_activity",
      FormatUid(runtime.remote_uid),
      runtime.last_flushed_transport_generation, pending));
  Log(ae::Format(
      "SYNC_RECONNECT_FLUSH peer={} generation={} pending_count={} "
      "reason=peer_activity",
      FormatUid(runtime.remote_uid),
      runtime.last_flushed_transport_generation, pending));
  Log(ae::Format(
      "CHAT_PENDING_FLUSH_BEGIN peer={} generation={} pending_count={} "
      "reason=peer_activity",
      FormatUid(runtime.remote_uid),
      runtime.last_flushed_transport_generation, pending));
  runtime.write_gate.Clear();
  runtime.last_retry = {};
  auto const now = last_tick_now_.has_value() ? *last_tick_now_ : ae::Now();
  DrivePending(runtime, now);
  runtime.flushed_for_peer_activity_ =
      runtime.session->pending_packet_count() == 0;
  Log(ae::Format(
      "CHAT_SYNC_RECONNECT_END peer={} generation={} pending_count={} "
      "reason=peer_activity",
      FormatUid(runtime.remote_uid),
      runtime.last_flushed_transport_generation,
      runtime.session->pending_packet_count()));
}

void ChatSyncController::NotifyTransportSessionReady(
    ae::Uid const& remote_uid, std::uint64_t transport_generation) {
  auto* runtime = FindRuntime(remote_uid);
  if (runtime == nullptr || runtime->session == nullptr) {
    return;
  }
  Log(ae::Format("CHAT_TRANSPORT_SESSION_READY peer={} generation={}",
                 FormatUid(remote_uid), transport_generation));
  // Do not pre-arm flushed_for_peer_activity_: if FlushPendingImmediate is
  // suppressed (same generation), peer-activity must still be able to clear
  // a write-gate cooldown after the remote recovers.
  FlushPendingImmediate(*runtime, transport_generation, "transport_generation");
}

void ChatSyncController::DrivePresence(RuntimeSession& runtime,
                                       ae::TimePoint now) {
  if (runtime.currently_online && runtime.last_seen.has_value() &&
      now - *runtime.last_seen >= timing_.offline_timeout) {
    ApplyOfflineTransition(runtime, "timeout");
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
    runtime.last_pending_count = 0;
    runtime.write_gate.Clear();
    runtime.flushed_for_peer_activity_ = false;
    runtime.flushed_for_stale_peer_ = false;
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

  // Host may restart without a new Client session generation and without an
  // immediate inbound packet. If the peer has been silent for a heartbeat
  // window while the write-gate is holding a cooldown, allow one immediate
  // re-offer (still at most once until last_seen moves again).
  if (!runtime.flushed_for_stale_peer_ && runtime.write_gate.size() > 0 &&
      runtime.last_seen.has_value() &&
      now - *runtime.last_seen >= timing_.heartbeat_interval) {
    runtime.flushed_for_stale_peer_ = true;
    Log(ae::Format(
        "CHAT_SYNC_RECONNECT_BEGIN peer={} generation={} pending_count={} "
        "reason=stale_peer",
        FormatUid(runtime.remote_uid),
        runtime.last_flushed_transport_generation, pending));
    Log(ae::Format(
        "CHAT_PENDING_FLUSH_BEGIN peer={} generation={} pending_count={} "
        "reason=stale_peer",
        FormatUid(runtime.remote_uid),
        runtime.last_flushed_transport_generation, pending));
    runtime.write_gate.Clear();
    runtime.last_retry = {};
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

  if (!runtime.write_gate.TryBegin(packet_id, now)) {
    // Gate still cooling down after a possibly-lost physical send. Allow a
    // later peer-activity flush to clear the cooldown once the remote is
    // delivering again.
    runtime.flushed_for_peer_activity_ = false;
    Log(ae::Format(
        "SYNC_WRITE_SUPPRESSED peer={} packet={} attempts={}",
        FormatUid(runtime.remote_uid), packet_id.id(),
        runtime.write_gate.attempt_count(packet_id)));
    return;
  }

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

  runtime->last_seen = now;
  runtime->flushed_for_stale_peer_ = false;
  ApplyOnlineTransition(*runtime);

  if (presence.has_value()) {
    FlushPendingOnPeerActivity(*runtime);
    return;
  }

  assert(runtime->session != nullptr);
  runtime->session->Receive(bytes);
  // Application ACK may have removed pending packets — drop gate slots now.
  if (runtime->session->pending_packet_count() == 0) {
    runtime->write_gate.Clear();
    runtime->last_pending_count = 0;
    runtime->flushed_for_peer_activity_ = false;
  } else {
    PruneWriteGate(*runtime);
    runtime->last_pending_count = runtime->session->pending_packet_count();
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

void ChatSyncController::Tick(ae::TimePoint now) {
  last_tick_now_ = now;
  DrainPendingAutoAccept();

  for (auto& runtime : sessions_) {
    assert(runtime.session != nullptr);
    runtime.session->Poll();
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
