#include "chat_sync_controller.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <string>
#include <utility>

#include "aether-miscpp/format/format.h"

#include "model/chat_peer_set.h"
#include "model/chat_presenter.h"
#include "startup_trace.h"

#include "apptraverse/runtime_trace.h"

namespace apptraverse::chat {
using ::apptraverse::Trace;
namespace {

std::string FormatUid(ae::Uid const& uid) { return ae::Format("{}", uid); }

bool ScheduleChanged(std::optional<PeerScheduleSnapshot> const& prev,
                     PeerScheduleSnapshot const& next) {
  if (!prev.has_value()) {
    return true;
  }
  return prev->schedule_state != next.schedule_state ||
         prev->next_ping_deadline.has_value() !=
             next.next_ping_deadline.has_value() ||
         (prev->next_ping_deadline.has_value() &&
          next.next_ping_deadline.has_value() &&
          *prev->next_ping_deadline != *next.next_ping_deadline) ||
         prev->last_online != next.last_online;
}

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

void ChatSyncController::SetIncomingPeerAuthorize(
    IncomingPeerAuthorizeFunction fn) {
  incoming_peer_authorize_ = std::move(fn);
}

void ChatSyncController::SetQueryPeerSchedule(QueryPeerScheduleFunction fn) {
  query_peer_schedule_ = std::move(fn);
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
  return GetPeerPresence(remote_uid) == PeerPresenceStatus::kOnline;
}

PeerPresenceStatus ChatSyncController::GetPeerPresence(
    ae::Uid const& remote_uid) const {
  if (auto const* runtime = FindRuntime(remote_uid)) {
    return runtime->presence;
  }
  return PeerPresenceStatus::kUnknown;
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

void ChatSyncController::SetPeerPresence(RuntimeSession& runtime,
                                         PeerPresenceStatus next,
                                         char const* reason) {
  auto const prev = runtime.presence;
  if (prev == next) {
    return;
  }
  runtime.presence = next;
  Log(ae::Format("PEER_PRESENCE peer={} from={} to={} reason={}",
                 FormatUid(runtime.remote_uid), PeerPresenceStatusName(prev),
                 PeerPresenceStatusName(next),
                 reason != nullptr ? reason : ""));
  if (next == PeerPresenceStatus::kOnline) {
    runtime.retry_skip_logged = false;
    runtime.pending_flush_done = false;
    FlushPendingOnOnline(runtime, last_tick_now_);
  }
  NotifyChanged();
}

void ChatSyncController::FlushPendingOnOnline(RuntimeSession& runtime,
                                              ae::TimePoint now) {
  assert(runtime.session != nullptr);
  if (runtime.pending_flush_done) {
    return;
  }
  if (runtime.session->pending_packet_count() == 0) {
    return;
  }
  runtime.pending_flush_done = true;
  Log(ae::Format("CHAT_PENDING_FLUSH peer={} pending={}",
                 FormatUid(runtime.remote_uid),
                 runtime.session->pending_packet_count()));
  runtime.session->RetryPending();
  runtime.last_retry = now;
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
    runtime.retry_skip_logged = false;
    runtime.pending_flush_done = false;
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

  // Presence Unknown/Offline blocks ONLY RetryPending here — not initial send.
  // StartOrResume already sends immediately regardless of presence.
  if (runtime.presence != PeerPresenceStatus::kOnline) {
    if (!runtime.retry_skip_logged) {
      runtime.retry_skip_logged = true;
      Log(ae::Format("CHAT_RETRY_SKIP peer={} presence={} pending={}",
                     FormatUid(runtime.remote_uid),
                     PeerPresenceStatusName(runtime.presence), pending));
    }
    return;
  }
  runtime.retry_skip_logged = false;

  bool const retry_due = runtime.last_retry.time_since_epoch().count() == 0 ||
                         now - runtime.last_retry >= timing_.retry_interval;
  if (retry_due) {
    Trace("SYNC_PACKET_SENT_FROM_TICK",
          ae::Format("peer={} pending={}", FormatUid(runtime.remote_uid),
                     pending));
    runtime.session->RetryPending();
    runtime.last_retry = now;
    Log(ae::Format("CHAT_RETRY_SENT peer={} pending={}",
                   FormatUid(runtime.remote_uid), pending));
  }
}

void ChatSyncController::RequestPeerSchedule(RuntimeSession& runtime) {
  // Presence queries are owned by the network PollPresence cycle.
  (void)runtime;
}

void ChatSyncController::RequestLocalSchedule() {
  // Presence queries are owned by the network PollPresence cycle.
}

void ChatSyncController::RefreshPresenceSchedules(ae::TimePoint now) {
  Trace("PRESENCE_TICK");
  // Network owns QueryPeerReceiveSchedule; keep throttle bookkeeping only.
  if (last_presence_refresh_.has_value() &&
      now - *last_presence_refresh_ < kPresenceRefreshInterval) {
    return;
  }
  last_presence_refresh_ = now;
}

void ChatSyncController::OnLocalScheduleResult(
    std::optional<PeerScheduleSnapshot> result) {
  auto const prev = local_presence_;
  if (result.has_value()) {
    local_schedule_ever_ok_ = true;
    if (ScheduleChanged(local_schedule_, *result)) {
      Log(ae::Format(
          "PEER_SCHEDULE peer=local state={} next_deadline={}",
          PeerScheduleStateName(result->schedule_state),
          result->next_ping_deadline.has_value() ? "yes" : "no"));
    }
    local_schedule_ = result;
    local_presence_ = ClassifyLocalPresence(true, result);
  } else if (local_schedule_ever_ok_) {
    local_presence_ = LocalPresenceStatus::kOffline;
  } else {
    local_presence_ = LocalPresenceStatus::kConnecting;
  }
  if (prev != local_presence_) {
    if (local_presence_ == LocalPresenceStatus::kOnline) {
      char const* reason = "other";
      if (result.has_value()) {
        // Online iff ClassifyLocalPresence saw PeerScheduleState::kExpected
        // from QueryPeerReceiveSchedule (Aether cloud schedule query).
        if (result->schedule_state == ae::PeerScheduleState::kExpected) {
          reason = "cloud_response";
        }
      }
      examples::StartupTrace(
          "LOCAL_ONLINE_TRIGGER",
          std::string("reason=") + reason +
              " source=ChatSyncController::OnLocalScheduleResult"
              " via=QueryPeerReceiveSchedule"
              " schedule_state=Expected");
      examples::StartupTrace("LOCAL_STATUS_ONLINE",
                             std::string("reason=") + reason);
    }
    Log(ae::Format("LOCAL_PRESENCE from={} to={}",
                   LocalPresenceStatusName(prev),
                   LocalPresenceStatusName(local_presence_)));
    NotifyChanged();
  }
}

void ChatSyncController::OnPeerScheduleResult(
    ae::Uid const& peer, std::optional<PeerScheduleSnapshot> result) {
  if (auto* runtime = FindRuntime(peer)) {
    ApplyPeerScheduleResult(*runtime, std::move(result));
  }
}

void ChatSyncController::ApplyPeerScheduleResult(
    RuntimeSession& runtime, std::optional<PeerScheduleSnapshot> result) {
  if (!result.has_value()) {
    SetPeerPresence(runtime, PeerPresenceStatus::kUnknown, "query_failed");
    return;
  }
  if (ScheduleChanged(runtime.last_schedule, *result)) {
    Log(ae::Format("PEER_SCHEDULE peer={} state={} next_deadline={}",
                   FormatUid(runtime.remote_uid),
                   PeerScheduleStateName(result->schedule_state),
                   result->next_ping_deadline.has_value() ? "yes" : "no"));
  }
  runtime.last_schedule = result;
  SetPeerPresence(runtime, ClassifyPeerPresence(*result), "schedule");
}

ChatSyncController::RuntimeSession& ChatSyncController::EnsureRuntimeSession(
    ae::Uid const& remote_uid, SyncSessionState::ptr state) {
  if (auto* existing = FindRuntime(remote_uid)) {
    return *existing;
  }

  assert(state.is_valid());
  state.Load();
  assert(state.is_loaded());

  Trace("SYNC_SESSION_CREATE_BEGIN",
        ae::Format("peer={}", FormatUid(remote_uid)));
  RuntimeSession runtime;
  runtime.remote_uid = remote_uid;
  // Session send callback always fires for new packets (Immediate). Retry
  // gating is applied only in DrivePending when presence is Online.
  runtime.session = std::make_unique<SharedGraphSyncSession>(
      replica_, state,
      [this, remote_uid](ae::ObjId packet_id, SerializedSyncPacket bytes) {
        send_(remote_uid, packet_id, bytes);
      });
  if (log_) {
    runtime.session->set_trace(log_);
  }
  runtime.last_initial_sync_complete =
      runtime.session->initial_sync_complete();
  sessions_.push_back(std::move(runtime));
  Trace("SYNC_SESSION_CREATE_END",
        ae::Format("peer={}", FormatUid(remote_uid)));
  return sessions_.back();
}

void ChatSyncController::Start() {
  peer_set_.Load();
  assert(peer_set_.is_loaded());
  for (auto& peer : peer_set_->peers) {
    assert(!peer.remote_uid.empty());
    assert(peer.session_state.is_valid());
    auto& runtime =
        EnsureRuntimeSession(peer.remote_uid, peer.session_state);
    EmitInitialMarkers(runtime);
    runtime.session->StartOrResume();
    if (runtime.session->initial_sync_complete() &&
        !runtime.last_initial_sync_complete) {
      runtime.last_initial_sync_complete = true;
      Log(ae::Format("CHAT_SYNC_INITIAL_COMPLETE peer={}",
                     FormatUid(runtime.remote_uid)));
    }
  }
  last_tick_now_ = ae::Now();
  last_presence_refresh_.reset();
  RefreshPresenceSchedules(last_tick_now_);
}

void ChatSyncController::Stop() {
  // No app-level presence heartbeat to withdraw.
}

SharedGraphSyncSession& ChatSyncController::AddPeer(ae::Uid const& remote_uid) {
  assert(!remote_uid.empty());
  if (auto* existing = FindSession(remote_uid)) {
    return *existing;
  }

  peer_set_.Load();
  assert(peer_set_.is_loaded());
  auto const& peer = AddChatPeer(peer_set_, chat_.id(), remote_uid);
  assert(peer.session_state.is_valid());
  Log(ae::Format("CHAT_PEER_ADDED uid={} session_state_id={}",
                 FormatUid(remote_uid), peer.session_state.id().id()));

  auto& runtime =
      EnsureRuntimeSession(remote_uid, peer.session_state);
  EmitInitialMarkers(runtime);
  auto const pending_before = runtime.session->pending_packet_count();
  runtime.session->StartOrResume();
  if (runtime.session->pending_packet_count() > pending_before) {
    Trace("SYNC_INITIAL_PACKET_CREATED",
          ae::Format("peer={} pending={}", FormatUid(remote_uid),
                     runtime.session->pending_packet_count()));
  }
  if (runtime.session->initial_sync_complete() &&
      !runtime.last_initial_sync_complete) {
    runtime.last_initial_sync_complete = true;
    Log(ae::Format("CHAT_SYNC_INITIAL_COMPLETE peer={}",
                   FormatUid(runtime.remote_uid)));
  }
  return *runtime.session;
}

bool ChatSyncController::IsIncomingAuthorized(ae::Uid const& remote_uid) const {
  if (!incoming_peer_authorize_) {
    return true;
  }
  return incoming_peer_authorize_(remote_uid);
}

void ChatSyncController::StashUnauthorized(
    ae::Uid const& remote_uid, std::vector<std::uint8_t> const& bytes) {
  for (auto& pending : pending_unauthorized_) {
    if (pending.remote_uid == remote_uid) {
      pending.packets.push_back(bytes);
      return;
    }
  }
  PendingUnauthorized pending;
  pending.remote_uid = remote_uid;
  pending.packets.push_back(bytes);
  pending_unauthorized_.push_back(std::move(pending));
  Log(ae::Format("CHAT_PEER_UNAUTHORIZED_STASH uid={} packets={}",
                 FormatUid(remote_uid), 1));
}

void ChatSyncController::ForwardAppliedEventsToOtherPeers(
    ae::Uid const& source_uid) {
  if (!host_event_relay_) {
    return;
  }
  for (auto& runtime : sessions_) {
    if (runtime.remote_uid == source_uid || runtime.session == nullptr) {
      continue;
    }
    auto const pending_before = runtime.session->pending_packet_count();
    // Poll discovers journal events not yet delivered to this peer (dedupe by
    // event id inside SharedGraphSyncSession).
    runtime.session->Poll();
    auto const pending_after = runtime.session->pending_packet_count();
    if (pending_after > pending_before) {
      Log(ae::Format(
          "CHAT_EVENT_FORWARDED source={} target={} pending_delta={}",
          FormatUid(source_uid), FormatUid(runtime.remote_uid),
          pending_after - pending_before));
    }
  }
}

void ChatSyncController::ReceiveKnown(ae::Uid const& remote_uid,
                                      std::vector<std::uint8_t> const& bytes) {
  auto* runtime = FindRuntime(remote_uid);
  assert(runtime != nullptr);
  assert(runtime->session != nullptr);
  runtime->session->Receive(bytes);

  if (!runtime->last_initial_sync_complete &&
      runtime->session->initial_sync_complete()) {
    runtime->last_initial_sync_complete = true;
    Log(ae::Format("CHAT_SYNC_INITIAL_COMPLETE peer={}",
                   FormatUid(runtime->remote_uid)));
  }

  ForwardAppliedEventsToOtherPeers(remote_uid);
  NotifyChanged();
}

void ChatSyncController::FlushUnauthorizedIfAuthorized() {
  if (pending_unauthorized_.empty()) {
    return;
  }
  std::vector<PendingUnauthorized> ready;
  pending_unauthorized_.erase(
      std::remove_if(
          pending_unauthorized_.begin(), pending_unauthorized_.end(),
          [&](PendingUnauthorized& pending) {
            if (!IsIncomingAuthorized(pending.remote_uid)) {
              return false;
            }
            ready.push_back(std::move(pending));
            return true;
          }),
      pending_unauthorized_.end());

  for (auto& pending : ready) {
    assert(!pending.remote_uid.empty());
    Log(ae::Format("CHAT_PEER_UNAUTHORIZED_FLUSH uid={} packets={}",
                   FormatUid(pending.remote_uid), pending.packets.size()));
    AddPeer(pending.remote_uid);
    for (auto const& packet : pending.packets) {
      ReceiveKnown(pending.remote_uid, packet);
    }
  }
}

void ChatSyncController::Receive(ae::Uid const& remote_uid,
                                 std::vector<std::uint8_t> const& bytes) {
  assert(!remote_uid.empty());
  if (FindRuntime(remote_uid) != nullptr) {
    ReceiveKnown(remote_uid, bytes);
    return;
  }
  if (!IsIncomingAuthorized(remote_uid)) {
    StashUnauthorized(remote_uid, bytes);
    return;
  }
  // Business-thread receive: AddPeer + ReceiveKnown immediately (no two-tick
  // QueueAutoAccept/armed/DrainPendingAutoAccept).
  AddPeer(remote_uid);
  ReceiveKnown(remote_uid, bytes);
}

void ChatSyncController::Tick(ae::TimePoint now) {
  Trace("SYNC_TICK");
  last_tick_now_ = now;
  // Tick only drives retry/presence — peer accept is push-based on Receive /
  // FlushUnauthorizedIfAuthorized.
  RefreshPresenceSchedules(now);

  for (auto& runtime : sessions_) {
    assert(runtime.session != nullptr);
    runtime.session->Poll();
    DrivePending(runtime, now);

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
