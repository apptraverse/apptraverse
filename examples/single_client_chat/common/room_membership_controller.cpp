#include "room_membership_controller.h"

#include <algorithm>

#include "aether-miscpp/format/format.h"

namespace apptraverse::chat {
namespace {

constexpr auto kRetryInterval = std::chrono::milliseconds{1000};
constexpr auto kAckTimeout = std::chrono::seconds{10};

std::string FormatUid(ae::Uid const& uid) { return ae::Format("{}", uid); }

ae::Uid ParseUid(std::string const& text) {
  return ae::Uid::FromString(std::string_view{text});
}

}  // namespace

RoomMembershipController::RoomMembershipController(
    ChatRoomRole role, ae::Uid local_uid, std::uint32_t local_client_obj_id,
    std::string local_name, ChatRoomLocalState::ptr state,
    RoomMembershipHooks hooks)
    : role_{role},
      local_uid_{local_uid},
      local_client_obj_id_{local_client_obj_id},
      local_name_{std::move(local_name)},
      state_{std::move(state)},
      hooks_{std::move(hooks)} {
  if (state_.is_loaded()) {
    applied_revision_ = state_->active_membership_revision;
    authorized_.clear();
    for (auto const& p : state_->active_participants) {
      if (p.uid.empty()) {
        continue;
      }
      RoomParticipantDesc d{};
      d.uid = ParseUid(p.uid);
      if (d.uid.empty()) {
        continue;
      }
      d.client_obj_id = p.client_obj_id;
      d.display_name = p.display_name;
      authorized_.push_back(std::move(d));
    }
    if (!state_->host_uid.empty()) {
      host_uid_ = ParseUid(state_->host_uid);
    }
  }
  if (role_ == ChatRoomRole::kHost) {
    SetStatus(RoomUiStatus::kActive);
  } else {
    SetStatus(RoomUiStatus::kDisconnected);
  }
}

std::string RoomMembershipController::UidKey(ae::Uid const& uid) {
  return FormatUid(uid);
}

void RoomMembershipController::SetStatus(RoomUiStatus status) {
  if (ui_status_ == status) {
    return;
  }
  auto const before = ui_status_;
  ui_status_ = status;
  Log(ae::Format("ROOM_UI_STATUS {}->{}", static_cast<int>(before),
                 static_cast<int>(status)));
  if (hooks_.on_ui_changed) {
    hooks_.on_ui_changed();
  }
}

void RoomMembershipController::SetError(std::string code) {
  error_ = std::move(code);
  SetStatus(RoomUiStatus::kError);
}

void RoomMembershipController::Log(std::string const& line) {
  if (hooks_.log) {
    hooks_.log(line);
  }
}

RoomParticipantDesc RoomMembershipController::MakeLocalDesc() const {
  RoomParticipantDesc d{};
  d.uid = local_uid_;
  d.client_obj_id = local_client_obj_id_;
  d.display_name = local_name_;
  return d;
}

void RoomMembershipController::PersistState() {
  if (!state_.is_loaded()) {
    return;
  }
  state_->role = role_;
  state_->local_client_obj_id = local_client_obj_id_;
  state_->local_display_name = local_name_;
  state_->host_uid = host_uid_.empty() ? std::string{} : FormatUid(host_uid_);
  state_->active_membership_revision = applied_revision_;
  state_->active_participants.clear();
  for (auto const& p : authorized_) {
    ChatRoomParticipantRecord rec{};
    rec.uid = FormatUid(p.uid);
    rec.client_obj_id = p.client_obj_id;
    rec.display_name = p.display_name;
    state_->active_participants.push_back(std::move(rec));
  }
  state_.Save();
}

std::vector<RoomParticipantDesc> RoomMembershipController::ActiveParticipants()
    const {
  return authorized_;
}

bool RoomMembershipController::IsAuthorizedSyncPeer(ae::Uid const& uid) const {
  if (uid.empty() || uid == local_uid_) {
    return false;
  }
  for (auto const& p : authorized_) {
    if (p.uid == uid) {
      return true;
    }
  }
  return false;
}

bool RoomMembershipController::FindActive(ae::Uid const& uid,
                                          std::uint32_t* client_obj_id,
                                          std::string* name) const {
  for (auto const& p : authorized_) {
    if (p.uid == uid) {
      if (client_obj_id) {
        *client_obj_id = p.client_obj_id;
      }
      if (name) {
        *name = p.display_name;
      }
      return true;
    }
  }
  return false;
}

void RoomMembershipController::HostReject(ae::Uid const& to,
                                          std::string reason) {
  RoomControlMessage rej{};
  rej.type = RoomControlType::kMembershipReject;
  rej.display_name = std::move(reason);
  if (hooks_.send_control) {
    hooks_.send_control(to, EncodeRoomControl(rej));
  }
}

std::vector<RoomParticipantDesc> RoomMembershipController::CandidateWith(
    PendingHello const& hello) const {
  auto list = authorized_;
  RoomParticipantDesc d{};
  d.uid = hello.uid;
  d.client_obj_id = hello.client_obj_id;
  d.display_name = hello.name;
  list.push_back(std::move(d));
  return list;
}

void RoomMembershipController::HostBootstrap() {
  if (role_ != ChatRoomRole::kHost) {
    return;
  }
  if (authorized_.empty()) {
    authorized_.push_back(MakeLocalDesc());
    applied_revision_ = 1;
    PersistState();
  }
  SetStatus(RoomUiStatus::kActive);
  host_phase_ = HostPhase::kIdle;
}

void RoomMembershipController::ClientConnect(ae::Uid host_uid) {
  if (role_ != ChatRoomRole::kClient || host_uid.empty()) {
    return;
  }
  if (ui_status_ != RoomUiStatus::kDisconnected &&
      ui_status_ != RoomUiStatus::kError) {
    return;
  }
  error_.clear();
  host_uid_ = host_uid;
  PersistState();
  if (hooks_.connect_peer) {
    hooks_.connect_peer(host_uid_);
  }
  RoomControlMessage hello{};
  hello.type = RoomControlType::kClientHello;
  hello.revision = 0;
  hello.client_obj_id = local_client_obj_id_;
  hello.display_name = local_name_;
  auto bytes = EncodeRoomControl(hello);
  last_control_payload_ = bytes;
  last_control_peer_ = host_uid_;
  last_control_broadcast_ = false;
  last_control_sent_ = ae::Now();
  if (hooks_.send_control) {
    hooks_.send_control(host_uid_, bytes);
  }
  SetStatus(RoomUiStatus::kConnecting);
}

void RoomMembershipController::HostSend(
    RoomControlType type, std::uint64_t revision,
    std::vector<RoomParticipantDesc> const& parts, ae::Uid const& to) {
  RoomControlMessage msg{};
  msg.type = type;
  msg.revision = revision;
  msg.participants = parts;
  auto bytes = EncodeRoomControl(msg);
  last_control_payload_ = bytes;
  last_control_peer_ = to;
  last_control_broadcast_ = false;
  last_control_sent_ = ae::Now();
  if (hooks_.send_control) {
    hooks_.send_control(to, bytes);
  }
}

void RoomMembershipController::HostBroadcast(
    RoomControlType type, std::uint64_t revision,
    std::vector<RoomParticipantDesc> const& parts) {
  RoomControlMessage msg{};
  msg.type = type;
  msg.revision = revision;
  msg.participants = parts;
  auto bytes = EncodeRoomControl(msg);
  last_control_payload_ = bytes;
  last_control_broadcast_ = true;
  last_control_sent_ = ae::Now();
  if (!hooks_.send_control) {
    return;
  }
  for (auto const& p : authorized_) {
    if (p.uid == local_uid_) {
      continue;
    }
    hooks_.send_control(p.uid, bytes);
  }
  // New candidate may not be in authorized_ yet during Prepare.
  if (type == RoomControlType::kMembershipPrepare ||
      (!current_hello_.uid.empty() &&
       std::none_of(authorized_.begin(), authorized_.end(), [&](auto const& p) {
         return p.uid == current_hello_.uid;
       }))) {
    if (!current_hello_.uid.empty() && current_hello_.uid != local_uid_) {
      hooks_.send_control(current_hello_.uid, bytes);
    }
  }
}

void RoomMembershipController::HostHandleReconnect(PendingHello const& hello) {
  activation_is_reconnect_ = true;
  current_hello_ = hello;
  candidate_revision_ = applied_revision_;
  candidate_list_ = authorized_;
  waiting_acks_.clear();
  waiting_acks_.insert(UidKey(hello.uid));
  HostSend(RoomControlType::kMembershipSnapshot, applied_revision_, authorized_,
           hello.uid);
  host_phase_ = HostPhase::kWaitApplied;
  activation_started_ = ae::Now();
  Log(ae::Format("ROOM_RECONNECT uid={}", FormatUid(hello.uid)));
}

void RoomMembershipController::HostHandleHello(ae::Uid const& from,
                                               RoomControlMessage const& msg) {
  if (msg.display_name.empty() ||
      msg.display_name.size() > kRoomControlMaxNameBytes ||
      msg.client_obj_id == 0) {
    HostReject(from, "invalid_hello");
    return;
  }

  // ClientHello is retried while Connecting. Deduplicate so one peer cannot
  // enqueue N activations / reconnects after the first succeeds.
  auto const already_pending = [&](ae::Uid const& uid) {
    if (current_hello_.uid == uid) {
      return true;
    }
    for (auto const& h : hello_queue_) {
      if (h.uid == uid) {
        return true;
      }
    }
    return false;
  };
  if (already_pending(from)) {
    return;
  }

  std::uint32_t existing_id = 0;
  std::string existing_name;
  if (FindActive(from, &existing_id, &existing_name)) {
    if (existing_id == msg.client_obj_id && existing_name == msg.display_name) {
      PendingHello hello{from, msg.client_obj_id, msg.display_name};
      if (host_phase_ == HostPhase::kIdle) {
        HostHandleReconnect(hello);
      } else {
        hello_queue_.push_back(hello);
      }
      return;
    }
    HostReject(from, "identity_mismatch");
    return;
  }

  for (auto const& p : authorized_) {
    if (p.client_obj_id == msg.client_obj_id && p.uid != from) {
      HostReject(from, "client_id_conflict");
      return;
    }
  }

  PendingHello hello{from, msg.client_obj_id, msg.display_name};
  hello_queue_.push_back(hello);
  HostStartNext();
}

void RoomMembershipController::HostStartNext() {
  if (role_ != ChatRoomRole::kHost || host_phase_ != HostPhase::kIdle) {
    return;
  }
  if (hello_queue_.empty()) {
    return;
  }
  current_hello_ = hello_queue_.front();
  hello_queue_.pop_front();

  std::uint32_t existing_id = 0;
  std::string existing_name;
  if (FindActive(current_hello_.uid, &existing_id, &existing_name) &&
      existing_id == current_hello_.client_obj_id &&
      existing_name == current_hello_.name) {
    HostHandleReconnect(current_hello_);
    return;
  }

  activation_is_reconnect_ = false;
  candidate_revision_ = applied_revision_ + 1;
  candidate_list_ = CandidateWith(current_hello_);
  waiting_acks_.clear();
  waiting_acks_.insert(UidKey(current_hello_.uid));
  if (hooks_.connect_peer) {
    hooks_.connect_peer(current_hello_.uid);
  }
  HostSend(RoomControlType::kMembershipPrepare, candidate_revision_,
           candidate_list_, current_hello_.uid);
  host_phase_ = HostPhase::kWaitPrepared;
  activation_started_ = ae::Now();
  Log(ae::Format("ROOM_ACTIVATION_BEGIN rev={} uid={}", candidate_revision_,
                 FormatUid(current_hello_.uid)));
}

void RoomMembershipController::HostOnPrepared(ae::Uid const& from,
                                              std::uint64_t revision) {
  if (host_phase_ != HostPhase::kWaitPrepared ||
      revision != candidate_revision_ || from != current_hello_.uid) {
    return;
  }
  // Commit participant + Join, then Snapshot.
  if (!activation_is_reconnect_) {
    if (hooks_.ensure_host_join) {
      hooks_.ensure_host_join(current_hello_.uid, current_hello_.client_obj_id,
                              current_hello_.name);
    }
    authorized_ = candidate_list_;
    applied_revision_ = candidate_revision_;
    PersistState();
    if (hooks_.on_model_changed) {
      hooks_.on_model_changed();
    }
  }
  waiting_acks_.clear();
  for (auto const& p : authorized_) {
    if (p.uid != local_uid_) {
      waiting_acks_.insert(UidKey(p.uid));
    }
  }
  HostBroadcast(RoomControlType::kMembershipSnapshot, applied_revision_,
                authorized_);
  host_phase_ = HostPhase::kWaitApplied;
  activation_started_ = ae::Now();
}

void RoomMembershipController::HostOnApplied(ae::Uid const& from,
                                             std::uint64_t revision) {
  if (host_phase_ != HostPhase::kWaitApplied) {
    return;
  }
  if (activation_is_reconnect_) {
    if (revision != applied_revision_ || from != current_hello_.uid) {
      return;
    }
  } else if (revision != applied_revision_) {
    return;
  }
  waiting_acks_.erase(UidKey(from));
  if (!waiting_acks_.empty()) {
    return;
  }
  waiting_acks_.clear();
  if (activation_is_reconnect_) {
    waiting_acks_.insert(UidKey(current_hello_.uid));
    HostSend(RoomControlType::kMembershipActivate, applied_revision_, {},
             current_hello_.uid);
  } else {
    for (auto const& p : authorized_) {
      if (p.uid != local_uid_) {
        waiting_acks_.insert(UidKey(p.uid));
      }
    }
    HostBroadcast(RoomControlType::kMembershipActivate, applied_revision_, {});
  }
  host_phase_ = HostPhase::kWaitActivated;
  activation_started_ = ae::Now();
}

void RoomMembershipController::HostOnActivated(ae::Uid const& from,
                                               std::uint64_t revision) {
  if (host_phase_ != HostPhase::kWaitActivated ||
      revision != applied_revision_) {
    return;
  }
  waiting_acks_.erase(UidKey(from));
  if (!waiting_acks_.empty()) {
    return;
  }
  HostFinishActivation();
}

void RoomMembershipController::HostFinishActivation() {
  // Ensure AddPeer for all authorized peers on host.
  if (hooks_.add_chat_peer) {
    for (auto const& p : authorized_) {
      if (p.uid != local_uid_) {
        hooks_.add_chat_peer(p.uid);
      }
    }
  }
  host_phase_ = HostPhase::kIdle;
  current_hello_ = {};
  last_control_payload_.clear();
  Log(ae::Format("ROOM_ACTIVATION_DONE rev={}", applied_revision_));
  HostStartNext();
}

void RoomMembershipController::ClientSend(RoomControlType type,
                                          std::uint64_t revision) {
  RoomControlMessage msg{};
  msg.type = type;
  msg.revision = revision;
  auto bytes = EncodeRoomControl(msg);
  last_control_payload_ = bytes;
  last_control_peer_ = host_uid_;
  last_control_broadcast_ = false;
  last_control_sent_ = ae::Now();
  if (hooks_.send_control) {
    hooks_.send_control(host_uid_, bytes);
  }
}

void RoomMembershipController::ClientApplySnapshot(
    RoomControlMessage const& msg) {
  if (msg.revision <= applied_revision_ && applied_revision_ != 0 &&
      msg.revision != applied_revision_) {
    // stale
    if (msg.revision < applied_revision_) {
      return;
    }
  }
  if (msg.revision < applied_revision_) {
    return;
  }
  authorized_ = msg.participants;
  applied_revision_ = msg.revision;
  PersistState();
  ClientSend(RoomControlType::kMembershipApplied, applied_revision_);
  SetStatus(RoomUiStatus::kWaitingForActivate);
}

void RoomMembershipController::ClientActivate(std::uint64_t revision) {
  if (revision != applied_revision_) {
    return;
  }
  // Host+1: only mesh with Host. No Client↔Client peer fanout.
  if (hooks_.add_chat_peer && !host_uid_.empty()) {
    hooks_.add_chat_peer(host_uid_);
  }
  ClientSend(RoomControlType::kMembershipActivated, applied_revision_);
  if (hooks_.has_local_join && hooks_.has_local_join()) {
    last_control_payload_.clear();
    SetStatus(RoomUiStatus::kActive);
  } else {
    SetStatus(RoomUiStatus::kWaitingForOwnJoin);
  }
}

void RoomMembershipController::NotifyLocalJoinAppeared() {
  if (role_ == ChatRoomRole::kClient &&
      ui_status_ == RoomUiStatus::kWaitingForOwnJoin) {
    last_control_payload_.clear();
    SetStatus(RoomUiStatus::kActive);
  }
}

void RoomMembershipController::OnControl(ae::Uid const& from,
                                         RoomControlMessage const& msg) {
  if (role_ == ChatRoomRole::kHost) {
    // Bootstrap: ClientHello is accepted before the peer is authorized for
    // Chat sync. Other control messages require an active or in-flight peer.
    if (msg.type != RoomControlType::kClientHello) {
      bool allowed = (from == current_hello_.uid) || FindActive(from, nullptr, nullptr);
      if (!allowed) {
        Log(ae::Format("ROOM_CONTROL_REJECT_UNKNOWN from={} type={}",
                       FormatUid(from), static_cast<int>(msg.type)));
        return;
      }
    }
    if (msg.type == RoomControlType::kClientHello) {
      HostHandleHello(from, msg);
      return;
    }
    if (msg.type == RoomControlType::kMembershipPrepared) {
      HostOnPrepared(from, msg.revision);
      return;
    }
    if (msg.type == RoomControlType::kMembershipApplied) {
      HostOnApplied(from, msg.revision);
      return;
    }
    if (msg.type == RoomControlType::kMembershipActivated) {
      HostOnActivated(from, msg.revision);
      return;
    }
    return;
  }

  // Client: only accept from configured host.
  if (host_uid_.empty() || from != host_uid_) {
    Log(ae::Format("ROOM_CONTROL_REJECT_NON_HOST from={}", FormatUid(from)));
    return;
  }
  switch (msg.type) {
    case RoomControlType::kMembershipPrepare:
      if (msg.revision == 0) {
        return;
      }
      if (ui_status_ == RoomUiStatus::kActive ||
          ui_status_ == RoomUiStatus::kWaitingForOwnJoin) {
        // Duplicate Hello-queue / reconnect noise: ACK without demoting.
        ClientSend(RoomControlType::kMembershipPrepared, msg.revision);
        break;
      }
      authorized_ = msg.participants;  // pre-authorized candidate
      ClientSend(RoomControlType::kMembershipPrepared, msg.revision);
      SetStatus(RoomUiStatus::kWaitingForSnapshot);
      break;
    case RoomControlType::kMembershipSnapshot:
      // Ignore snapshot while already active/waiting-join for this revision
      // (duplicate reconnect / Hello-queue noise must not demote Active).
      if ((ui_status_ == RoomUiStatus::kActive ||
           ui_status_ == RoomUiStatus::kWaitingForOwnJoin) &&
          msg.revision == applied_revision_) {
        ClientSend(RoomControlType::kMembershipApplied, applied_revision_);
        break;
      }
      ClientApplySnapshot(msg);
      break;
    case RoomControlType::kMembershipActivate:
      if (ui_status_ == RoomUiStatus::kWaitingForActivate ||
          ui_status_ == RoomUiStatus::kWaitingForSnapshot) {
        ClientActivate(msg.revision);
      } else if (ui_status_ == RoomUiStatus::kWaitingForOwnJoin ||
                 ui_status_ == RoomUiStatus::kActive) {
        // Idempotent ACK while host retries Activate until HostFinishActivation.
        if (msg.revision == applied_revision_) {
          ClientSend(RoomControlType::kMembershipActivated, applied_revision_);
        }
      }
      break;
    case RoomControlType::kMembershipReject:
      SetError(msg.display_name.empty() ? "membership_rejected"
                                        : msg.display_name);
      break;
    default:
      break;
  }
}

void RoomMembershipController::Tick(ae::TimePoint now) {
  if (last_control_payload_.empty()) {
    if (role_ == ChatRoomRole::kHost) {
      HostStartNext();
    }
    return;
  }
  if (role_ == ChatRoomRole::kHost && host_phase_ != HostPhase::kIdle) {
    if (now - activation_started_ > kAckTimeout) {
      Log(ae::Format("ROOM_ACTIVATION_TIMEOUT phase={} rev={}",
                     static_cast<int>(host_phase_), applied_revision_));
      // Host stays Active for chat; abort only the in-flight activation so a
      // late Hello can restart. Do not demote host UI to Error.
      host_phase_ = HostPhase::kIdle;
      last_control_payload_.clear();
      current_hello_ = {};
      activation_is_reconnect_ = false;
      HostStartNext();
      return;
    }
  }
  if (now - last_control_sent_ < kRetryInterval) {
    return;
  }
  last_control_sent_ = now;
  if (!hooks_.send_control) {
    return;
  }
  if (last_control_broadcast_) {
    for (auto const& p : authorized_) {
      if (p.uid == local_uid_) {
        continue;
      }
      hooks_.send_control(p.uid, last_control_payload_);
    }
    if (!current_hello_.uid.empty()) {
      hooks_.send_control(current_hello_.uid, last_control_payload_);
    }
  } else if (!last_control_peer_.empty()) {
    hooks_.send_control(last_control_peer_, last_control_payload_);
  }
}

}  // namespace apptraverse::chat
