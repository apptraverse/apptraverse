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
                                       SendFunction send,
                                       ReconnectFunction reconnect,
                                       ChatSyncTiming timing,
                                       bool auto_accept_incoming,
                                       ChangedFunction changed,
                                       LogFunction log)
    : replica_{replica},
      chat_{std::move(chat)},
      peer_set_{std::move(peer_set)},
      send_{std::move(send)},
      reconnect_{std::move(reconnect)},
      timing_{timing},
      auto_accept_incoming_{auto_accept_incoming},
      changed_{std::move(changed)},
      log_{std::move(log)} {
  assert(send_);
  if (!reconnect_) {
    reconnect_ = [](ae::Uid const&) {};
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

void ChatSyncController::DrivePending(RuntimeSession& runtime,
                                      ae::TimePoint now) {
  assert(runtime.session != nullptr);
  auto const pending = runtime.session->pending_packet_count();

  if (pending == 0) {
    if (runtime.last_pending_count > 0) {
      Log(ae::Format("CHAT_PENDING_CHANGED peer={} pending=0",
                     FormatUid(runtime.remote_uid)));
    }
    runtime.last_reconnect = {};
    runtime.last_pending_count = 0;
    return;
  }

  if (runtime.last_pending_count == 0) {
    // Pending went 0 → N: start reconnect timer.
    runtime.last_reconnect = now;
    Log(ae::Format("CHAT_PENDING_CHANGED peer={} pending={}",
                   FormatUid(runtime.remote_uid), pending));
  }
  runtime.last_pending_count = pending;

  bool const retry_due = runtime.last_retry.time_since_epoch().count() == 0 ||
                         now - runtime.last_retry >= timing_.retry_interval;
  if (retry_due) {
    runtime.session->RetryPending();
    runtime.last_retry = now;
    Log(ae::Format("CHAT_RETRY_SENT peer={} pending={}",
                   FormatUid(runtime.remote_uid), pending));
  }

  if (runtime.last_reconnect.time_since_epoch().count() != 0 &&
      now - runtime.last_reconnect >= timing_.reconnect_interval) {
    reconnect_(runtime.remote_uid);
    runtime.session->RetryPending();
    runtime.last_retry = now;
    runtime.last_reconnect = now;
    Log(ae::Format("CHAT_RECONNECT peer={} pending={}",
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
        send_(remote_uid, packet_id, bytes);
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

}  // namespace apptraverse::examples
