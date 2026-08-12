#include "chat_sync_controller.h"

#include <cassert>
#include <utility>

#include "aether-miscpp/format/format.h"

#include "model/chat_peer_set.h"
#include "model/chat_presenter.h"

namespace apptraverse::examples {
namespace {

std::string FormatUid(ae::Uid const& uid) { return ae::Format("{}", uid); }

}  // namespace

ChatSyncController::ChatSyncController(SyncReplica replica, Chat::ptr chat,
                                       ChatPeerSet::ptr peer_set,
                                       SyncTransportOperations ops,
                                       SyncRecoveryPolicy policy,
                                       bool auto_accept_incoming,
                                       ChangedFunction changed,
                                       LogFunction log)
    : replica_{replica},
      chat_{std::move(chat)},
      peer_set_{std::move(peer_set)},
      ops_{std::move(ops)},
      policy_{policy},
      auto_accept_incoming_{auto_accept_incoming},
      changed_{std::move(changed)},
      log_{std::move(log)} {
  assert(ops_.send);
  if (!ops_.ensure_outgoing) {
    ops_.ensure_outgoing = [](ae::Uid const&) {};
  }
  if (!ops_.outgoing_state) {
    ops_.outgoing_state = [](ae::Uid const&) {
      return P2pOutgoingState::kWritable;
    };
  }
  if (!ops_.restream_outgoing) {
    ops_.restream_outgoing = [](ae::Uid const&) {};
  }
  if (!ops_.replace_outgoing) {
    ops_.replace_outgoing = [](ae::Uid const&) {};
  }
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
  for (auto& runtime : sessions_) {
    if (runtime.remote_uid == remote_uid) {
      return runtime.session.get();
    }
  }
  return nullptr;
}

SharedGraphSyncSession const* ChatSyncController::FindSession(
    ae::Uid const& remote_uid) const {
  for (auto const& runtime : sessions_) {
    if (runtime.remote_uid == remote_uid) {
      return runtime.session.get();
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

void ChatSyncController::ResetRecoveryCycle(RuntimeSession& runtime,
                                            ae::TimePoint now) {
  (void)now;
  runtime.pending_since.reset();
  runtime.restream_done_for_current_stall = false;
  runtime.replace_done_for_current_stall = false;
  runtime.last_ack_progress_revision =
      runtime.session->ack_progress_revision();
}

void ChatSyncController::DriveRecovery(RuntimeSession& runtime,
                                       ae::TimePoint now) {
  assert(runtime.session != nullptr);
  auto const pending = runtime.session->pending_packet_count();
  auto const ack_rev = runtime.session->ack_progress_revision();

  if (ack_rev != runtime.last_ack_progress_revision || pending == 0) {
    if (runtime.last_pending_count > 0 && pending == 0) {
      Log(ae::Format("CHAT_PENDING_CLEARED peer={}",
                     FormatUid(runtime.remote_uid)));
    }
    ResetRecoveryCycle(runtime, now);
    runtime.last_pending_count = pending;
    if (pending == 0) {
      return;
    }
  }
  runtime.last_pending_count = pending;

  if (!runtime.pending_since.has_value()) {
    runtime.pending_since = now;
  }

  ops_.ensure_outgoing(runtime.remote_uid);
  auto const state = ops_.outgoing_state(runtime.remote_uid);
  bool const writable = state == P2pOutgoingState::kWritable;
  auto const stalled_for = now - *runtime.pending_since;

  // Retry only while the active outgoing can accept bytes.
  if (writable) {
    bool const due = runtime.last_retry.time_since_epoch().count() == 0 ||
                     now - runtime.last_retry >= policy_.retry_interval;
    if (due) {
      runtime.session->RetryPending();
      runtime.last_retry = now;
      Log(ae::Format("CHAT_RETRY_SENT peer={} pending={}",
                     FormatUid(runtime.remote_uid), pending));
    }
  } else {
    bool const due =
        runtime.last_retry_gate_log.time_since_epoch().count() == 0 ||
        now - runtime.last_retry_gate_log >= policy_.retry_interval;
    if (due) {
      runtime.last_retry_gate_log = now;
      Log(ae::Format("CHAT_RETRY_GATED peer={} pending={}",
                     FormatUid(runtime.remote_uid), pending));
    }
  }

  // Error: restream promptly on the next Tick without waiting restream_after.
  if (state == P2pOutgoingState::kError &&
      !runtime.restream_done_for_current_stall) {
    ops_.restream_outgoing(runtime.remote_uid);
    runtime.restream_done_for_current_stall = true;
    runtime.last_restream_time = now;
    Log(ae::Format("CHAT_ACK_STALL_RESTREAM peer={} pending={}",
                   FormatUid(runtime.remote_uid), pending));
  } else if (!runtime.restream_done_for_current_stall &&
             stalled_for >= policy_.restream_after) {
    ops_.restream_outgoing(runtime.remote_uid);
    runtime.restream_done_for_current_stall = true;
    runtime.last_restream_time = now;
    Log(ae::Format("CHAT_ACK_STALL_RESTREAM peer={} pending={}",
                   FormatUid(runtime.remote_uid), pending));
  }

  if (!runtime.replace_done_for_current_stall &&
      stalled_for >= policy_.replace_after) {
    ops_.replace_outgoing(runtime.remote_uid);
    runtime.replace_done_for_current_stall = true;
    runtime.last_replace_time = now;
    Log(ae::Format("CHAT_ACK_STALL_REPLACE peer={} pending={}",
                   FormatUid(runtime.remote_uid), pending));
  }
}

ChatSyncController::RuntimeSession& ChatSyncController::EnsureRuntimeSession(
    ae::Uid const& remote_uid, SyncSessionState::ptr state) {
  if (auto* existing = FindSession(remote_uid)) {
    for (auto& runtime : sessions_) {
      if (runtime.remote_uid == remote_uid) {
        return runtime;
      }
    }
    (void)existing;
  }

  assert(state.is_valid());
  state.Load();
  assert(state.is_loaded());

  RuntimeSession runtime;
  runtime.remote_uid = remote_uid;
  runtime.session = std::make_unique<SharedGraphSyncSession>(
      replica_, state,
      [this, remote_uid](ae::ObjId packet_id, SerializedSyncPacket bytes) {
        ops_.send(remote_uid, packet_id, bytes);
      });
  runtime.last_initial_sync_complete =
      runtime.session->initial_sync_complete();
  runtime.last_ack_progress_revision =
      runtime.session->ack_progress_revision();
  sessions_.push_back(std::move(runtime));
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
  ops_.ensure_outgoing(remote_uid);
  runtime.session->StartOrResume();
  if (runtime.session->initial_sync_complete() &&
      !runtime.last_initial_sync_complete) {
    runtime.last_initial_sync_complete = true;
    Log(ae::Format("CHAT_SYNC_INITIAL_COMPLETE peer={}",
                   FormatUid(runtime.remote_uid)));
  }
  return *runtime.session;
}

void ChatSyncController::Receive(ae::Uid const& remote_uid,
                                 SerializedSyncPacket const& bytes) {
  assert(!remote_uid.empty());
  auto* session = FindSession(remote_uid);
  if (session == nullptr) {
    if (!auto_accept_incoming_) {
      Log(ae::Format("CHAT_PEER_REJECTED uid={}", FormatUid(remote_uid)));
      return;
    }
    AddPeer(remote_uid);
    session = FindSession(remote_uid);
    assert(session != nullptr);
  }

  session->Receive(bytes);

  for (auto& runtime : sessions_) {
    if (runtime.remote_uid != remote_uid) {
      continue;
    }
    if (!runtime.last_initial_sync_complete &&
        runtime.session->initial_sync_complete()) {
      runtime.last_initial_sync_complete = true;
      Log(ae::Format("CHAT_SYNC_INITIAL_COMPLETE peer={}",
                     FormatUid(runtime.remote_uid)));
    }
  }

  NotifyChanged();
}

void ChatSyncController::Tick(ae::TimePoint now) {
  for (auto& runtime : sessions_) {
    assert(runtime.session != nullptr);
    runtime.session->Poll();
    DriveRecovery(runtime, now);

    if (!runtime.last_initial_sync_complete &&
        runtime.session->initial_sync_complete()) {
      runtime.last_initial_sync_complete = true;
      Log(ae::Format("CHAT_SYNC_INITIAL_COMPLETE peer={}",
                     FormatUid(runtime.remote_uid)));
      NotifyChanged();
    }
  }
}

}  // namespace apptraverse::examples
