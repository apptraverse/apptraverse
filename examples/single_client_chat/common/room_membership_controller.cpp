#include "room_membership_controller.h"

#include <algorithm>
#include <string_view>

#include "aether-miscpp/format/format.h"

#include "apptraverse/runtime_trace.h"

namespace apptraverse::chat {
using ::apptraverse::AssertBusinessThread;
using ::apptraverse::Trace;
namespace {

// Lost application/control packet retry: at most once per presence cycle.
constexpr auto kRetryInterval = std::chrono::seconds{3};

std::string FormatUid(ae::Uid const& uid) { return ae::Format("{}", uid); }

ae::Uid ParseUid(std::string const& text) {
  return ae::Uid::FromString(std::string_view{text});
}

char const* RoomUiStatusLabel(RoomUiStatus status) {
  switch (status) {
    case RoomUiStatus::kDisconnected:
      return "Disconnected";
    case RoomUiStatus::kConnecting:
      return "Connecting";
    case RoomUiStatus::kSyncing:
      return "Syncing";
    case RoomUiStatus::kWaitingForOwnJoin:
      return "WaitingForOwnJoin";
    case RoomUiStatus::kActive:
      return "Active";
    case RoomUiStatus::kError:
      return "Error";
  }
  return "Unknown";
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
  bool const was_active = before == RoomUiStatus::kActive;
  ui_status_ = status;
  bool const now_active = status == RoomUiStatus::kActive;
  Log(ae::Format("ROOM_STATE_CHANGED old={} new={}", RoomUiStatusLabel(before),
                 RoomUiStatusLabel(status)));
  if (was_active != now_active) {
    Log(ae::Format("ROOM_ACTIVE_CHANGED {}->{}", was_active ? "true" : "false",
                   now_active ? "true" : "false"));
  }
  if (now_active) {
    Trace("ROOM_ACTIVE",
          ae::Format("old={} new={}", RoomUiStatusLabel(before),
                     RoomUiStatusLabel(status)));
    Log("ROOM_ACTIVE");
  }
  if (hooks_.on_ui_changed) {
    hooks_.on_ui_changed();
  }
}

void RoomMembershipController::EnsureClientActiveIfJoined(
    char const* reason, bool refresh_active_ui) {
  if (role_ != ChatRoomRole::kClient) {
    return;
  }
  bool const waiting = ui_status_ == RoomUiStatus::kWaitingForOwnJoin ||
                       ui_status_ == RoomUiStatus::kConnecting ||
                       ui_status_ == RoomUiStatus::kSyncing;

  RoomLocalJoinIdentity identity{};
  if (hooks_.probe_local_join) {
    identity = hooks_.probe_local_join();
  } else if (hooks_.has_local_join && hooks_.has_local_join()) {
    identity.obj_id_match = true;
    identity.local_client_obj_id = local_client_obj_id_;
    identity.join_client_obj_id = local_client_obj_id_;
  }

  if (identity.name_fallback && !identity.obj_id_match) {
    Log(ae::Format(
        "JOIN_IDENTITY_FALLBACK_USED local_client_obj_id={} "
        "join_client_obj_id={}",
        identity.local_client_obj_id, identity.join_client_obj_id));
    return;
  }
  if (!identity.obj_id_match) {
    return;
  }

  if (waiting) {
    Log(ae::Format(
        "JOIN_IDENTITY_MATCH local_client_obj_id={} join_client_obj_id={}",
        identity.local_client_obj_id != 0 ? identity.local_client_obj_id
                                          : local_client_obj_id_,
        identity.join_client_obj_id != 0 ? identity.join_client_obj_id
                                         : local_client_obj_id_));
    Log("ROOM_OWN_JOIN_DETECTED");
    Log("LOCAL_JOIN");
    ClearLastControl();
    SetStatus(RoomUiStatus::kActive);
    if (reason != nullptr) {
      Log(ae::Format("ROOM_ACTIVE reason={}", reason));
    }
    return;
  }
  if (refresh_active_ui && ui_status_ == RoomUiStatus::kActive) {
    if (hooks_.on_ui_changed) {
      hooks_.on_ui_changed();
    }
  }
}

void RoomMembershipController::SetError(std::string code) {
  error_ = std::move(code);
  ClearLastControl();
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

std::vector<RoomParticipantDesc> const&
RoomMembershipController::ActiveParticipants() const {
  return authorized_;
}

bool RoomMembershipController::IsAuthorizedSyncPeer(ae::Uid const& uid) const {
  if (uid.empty() || uid == local_uid_) {
    return false;
  }
  // Client: only the host, once Connecting+.
  if (role_ == ChatRoomRole::kClient) {
    if (host_uid_.empty() || uid != host_uid_) {
      return false;
    }
    return ui_status_ != RoomUiStatus::kDisconnected &&
           ui_status_ != RoomUiStatus::kError;
  }
  // Host: all authorized remote participants.
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

void RoomMembershipController::RememberLastControl(
    ae::Uid const& peer, std::vector<std::uint8_t> bytes) {
  last_control_payload_ = std::move(bytes);
  last_control_peer_ = peer;
  last_control_sent_ = ae::Now();
}

void RoomMembershipController::ClearLastControl() {
  last_control_payload_.clear();
  last_control_peer_ = {};
  pending_join_request_id_ = 0;
}

std::uint64_t RoomMembershipController::NextRequestId() {
  return next_request_id_++;
}

void RoomMembershipController::HostReject(ae::Uid const& to,
                                          std::uint64_t request_id,
                                          std::string reason) {
  RoomControlMessage rej{};
  rej.type = RoomControlType::kJoinRoomRejected;
  rej.request_id = request_id;
  rej.display_name = std::move(reason);
  auto bytes = EncodeRoomControl(rej);
  if (hooks_.send_control) {
    hooks_.send_control(to, bytes);
  }
  Log(ae::Format("ROOM_JOIN_REJECTED to={} request_id={} reason={}",
                 FormatUid(to), request_id, rej.display_name));
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
  Log(ae::Format("ROOM_HOST_BOOTSTRAP rev={} participants={}",
                 applied_revision_, authorized_.size()));
}

void RoomMembershipController::ClientSendJoinRequest() {
  RoomControlMessage req{};
  req.type = RoomControlType::kJoinRoomRequest;
  req.revision = 0;
  req.request_id = pending_join_request_id_;
  req.client_obj_id = local_client_obj_id_;
  req.display_name = local_name_;
  Trace("JOIN_REQUEST_CREATED",
        ae::Format("peer={} request_id={} client_obj_id={}", host_uid_,
                   pending_join_request_id_, local_client_obj_id_));
  auto bytes = EncodeRoomControl(req);
  RememberLastControl(host_uid_, bytes);
  if (hooks_.send_control) {
    Trace("JOIN_REQUEST_NETWORK_ENQUEUE",
          ae::Format("peer={} request_id={} size={}", host_uid_,
                     pending_join_request_id_, bytes.size()));
    hooks_.send_control(host_uid_, bytes);
  }
  Log(ae::Format("ROOM_JOIN_REQUEST sent request_id={} host={}",
                 pending_join_request_id_, FormatUid(host_uid_)));
}

void RoomMembershipController::ClientConnect(ae::Uid host_uid) {
  AssertBusinessThread("RoomMembershipController::ClientConnect");
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
  SetStatus(RoomUiStatus::kConnecting);
  if (hooks_.connect_peer) {
    Trace("JOIN_CONNECT_PEER_ENQUEUE",
          ae::Format("peer={}", host_uid_));
    hooks_.connect_peer(host_uid_);
  }
  pending_join_request_id_ = NextRequestId();
  ClientSendJoinRequest();
}

void RoomMembershipController::OnTransportSessionReady(ae::Uid const& peer) {
  // SessionReady must ONLY flush FIFO frames on the transport. Do not resend
  // last_control — Tick handles lost-packet retry after timeout.
  Log(ae::Format("ROOM_TRANSPORT_READY peer={} (no control resend)",
                 FormatUid(peer)));
}

void RoomMembershipController::HostBroadcastParticipantsChanged(
    ae::Uid const& except) {
  RoomControlMessage msg{};
  msg.type = RoomControlType::kParticipantsChanged;
  msg.revision = applied_revision_;
  msg.request_id = 0;
  msg.participants = authorized_;
  auto bytes = EncodeRoomControl(msg);
  if (!hooks_.send_control) {
    return;
  }
  for (auto const& p : authorized_) {
    if (p.uid == local_uid_ || p.uid == except) {
      continue;
    }
    hooks_.send_control(p.uid, bytes);
    Log(ae::Format("ROOM_PARTICIPANTS_CHANGED to={} rev={}", FormatUid(p.uid),
                   applied_revision_));
  }
}

void RoomMembershipController::HostHandleJoinRequest(
    ae::Uid const& from, RoomControlMessage const& msg) {
  AssertBusinessThread("RoomMembershipController::HostHandleJoinRequest");
  Trace("HOST_JOIN_PROCESS_BEGIN",
        ae::Format("peer={} request_id={} client_obj_id={}", from, msg.request_id,
                   msg.client_obj_id));
  if (msg.display_name.empty() ||
      msg.display_name.size() > kRoomControlMaxNameBytes ||
      msg.client_obj_id == 0 || msg.request_id == 0) {
    HostReject(from, msg.request_id, "invalid_join");
    Trace("HOST_JOIN_PROCESS_END",
          ae::Format("peer={} request_id={} result=reject", from, msg.request_id));
    return;
  }

  Log(ae::Format("ROOM_JOIN_REQUEST from={} request_id={} obj_id={} name={}",
                 FormatUid(from), msg.request_id, msg.client_obj_id,
                 msg.display_name));

  std::uint32_t existing_id = 0;
  std::string existing_name;
  bool const already = FindActive(from, &existing_id, &existing_name);
  bool const same_identity =
      already && existing_id == msg.client_obj_id &&
      existing_name == msg.display_name;
  bool const reconnect = same_identity;

  if (already && !same_identity) {
    HostReject(from, msg.request_id, "identity_mismatch");
    Trace("HOST_JOIN_PROCESS_END",
          ae::Format("peer={} request_id={} result=reject", from, msg.request_id));
    return;
  }

  for (auto const& p : authorized_) {
    if (p.client_obj_id == msg.client_obj_id && p.uid != from) {
      HostReject(from, msg.request_id, "client_id_conflict");
      Trace("HOST_JOIN_PROCESS_END",
            ae::Format("peer={} request_id={} result=reject", from,
                       msg.request_id));
      return;
    }
  }

  // Host + one Client only.
  if (!already) {
    std::size_t remote_count = 0;
    for (auto const& p : authorized_) {
      if (p.uid != local_uid_) {
        ++remote_count;
      }
    }
    if (remote_count >= 1) {
      HostReject(from, msg.request_id, "room_full");
      Trace("HOST_JOIN_PROCESS_END",
            ae::Format("peer={} request_id={} result=room_full", from,
                       msg.request_id));
      return;
    }
  }

  if (!already) {
    RoomParticipantDesc d{};
    d.uid = from;
    d.client_obj_id = msg.client_obj_id;
    d.display_name = msg.display_name;
    authorized_.push_back(std::move(d));
    applied_revision_ += 1;
    PersistState();
    Log(ae::Format("ROOM_PARTICIPANT_ADDED uid={} rev={}", FormatUid(from),
                   applied_revision_));
  }

  if (hooks_.connect_peer) {
    Trace("JOIN_CONNECT_PEER_ENQUEUE", ae::Format("peer={}", from));
    hooks_.connect_peer(from);
  }

  // Join must land in the journal before AddPeer/StartOrResume so the initial
  // NodeState includes it. PublishCommittedEvent is a no-op until initial sync
  // completes, so EventPacket-after-AddPeer leaves clients stuck WaitingForOwnJoin.
  if (!reconnect && hooks_.ensure_host_join) {
    Trace("HOST_CLIENT_OBJECT",
          ae::Format("peer={} client_obj_id={}", from, msg.client_obj_id));
    hooks_.ensure_host_join(from, msg.client_obj_id, msg.display_name);
  }

  // Star: host AddPeer(client) only — never client-to-client.
  if (hooks_.add_chat_peer) {
    Trace("HOST_CHAT_ADD_PEER_BEGIN", ae::Format("peer={}", from));
    hooks_.add_chat_peer(from);
    Trace("HOST_CHAT_ADD_PEER_END", ae::Format("peer={}", from));
  }

  RoomControlMessage accept{};
  accept.type = RoomControlType::kJoinRoomAccepted;
  accept.revision = applied_revision_;
  accept.request_id = msg.request_id;
  accept.participants = authorized_;
  Trace("JOIN_ACCEPTED_CREATED",
        ae::Format("peer={} request_id={} revision={}", from, msg.request_id,
                   applied_revision_));
  auto bytes = EncodeRoomControl(accept);
  // Keep Accept as last_control until JoinAcceptedAck (or client Join retry).
  // Primary send is immediate; Tick retries at most once per 3s if lost.
  if (hooks_.send_control) {
    Trace("JOIN_ACCEPTED_NETWORK_ENQUEUE",
          ae::Format("peer={} request_id={} size={}", from, msg.request_id,
                     bytes.size()));
    hooks_.send_control(from, bytes);
    RememberLastControl(from, bytes);
  }
  Log(ae::Format("ROOM_JOIN_ACCEPTED to={} request_id={} rev={} reconnect={}",
                 FormatUid(from), msg.request_id, applied_revision_,
                 reconnect ? 1 : 0));

  if (!reconnect) {
    // Host+1 client: no fan-out ParticipantsChanged to other remotes.
  }

  if (hooks_.on_model_changed) {
    hooks_.on_model_changed();
  }
  if (hooks_.on_ui_changed) {
    Trace("HOST_UI_PARTICIPANTS_DIRTY", ae::Format("peer={}", from));
    hooks_.on_ui_changed();
  }
  Trace("HOST_JOIN_PROCESS_END",
        ae::Format("peer={} request_id={} result=ok", from, msg.request_id));
}

void RoomMembershipController::ClientApplyParticipants(
    RoomControlMessage const& msg) {
  if (msg.revision < applied_revision_) {
    return;
  }
  authorized_ = msg.participants;
  applied_revision_ = msg.revision;
  PersistState();
  Trace("CLIENT_PARTICIPANTS_APPLIED",
        ae::Format("revision={} count={}", applied_revision_,
                   authorized_.size()));
  Log(ae::Format("ROOM_PARTICIPANTS_APPLIED rev={} count={}", applied_revision_,
                 authorized_.size()));
}

void RoomMembershipController::ClientOnJoinAccepted(
    RoomControlMessage const& msg) {
  if (pending_join_request_id_ != 0 &&
      msg.request_id != pending_join_request_id_) {
    Log(ae::Format("ROOM_JOIN_ACCEPTED_STALE request_id={} pending={}",
                   msg.request_id, pending_join_request_id_));
    return;
  }
  Log(ae::Format("ROOM_JOIN_ACCEPTED rx request_id={} rev={}", msg.request_id,
                 msg.revision));

  bool const already_settled =
      (ui_status_ == RoomUiStatus::kActive ||
       ui_status_ == RoomUiStatus::kWaitingForOwnJoin) &&
      msg.revision <= applied_revision_ && applied_revision_ != 0;

  if (!already_settled) {
    if (ui_status_ == RoomUiStatus::kConnecting ||
        ui_status_ == RoomUiStatus::kDisconnected ||
        ui_status_ == RoomUiStatus::kError) {
      SetStatus(RoomUiStatus::kSyncing);
    }
    ClientApplyParticipants(msg);
  } else if (msg.revision > applied_revision_) {
    ClientApplyParticipants(msg);
  }

  if (hooks_.add_chat_peer && !host_uid_.empty()) {
    Trace("CLIENT_CHAT_ADD_PEER_BEGIN", ae::Format("peer={}", host_uid_));
    hooks_.add_chat_peer(host_uid_);
    Trace("CLIENT_CHAT_ADD_PEER_END", ae::Format("peer={}", host_uid_));
  }

  ClearLastControl();

  Trace("LOCAL_JOIN_CHECK",
        ae::Format("request_id={} has_hook={}", msg.request_id,
                   hooks_.has_local_join ? 1 : 0));
  bool const has_join = hooks_.has_local_join && hooks_.has_local_join();
  if (has_join) {
    Trace("LOCAL_JOIN_DETECTED", ae::Format("request_id={}", msg.request_id));
    if (ui_status_ != RoomUiStatus::kActive) {
      Trace("ROOM_ACTIVE", ae::Format("reason=join_accepted"));
      SetStatus(RoomUiStatus::kActive);
    }
  } else if (ui_status_ != RoomUiStatus::kActive) {
    SetStatus(RoomUiStatus::kWaitingForOwnJoin);
  }
  EnsureClientActiveIfJoined("join_accepted", true);

  // Optional non-blocking ACK — must not gate AddPeer/sync/UI.
  RoomControlMessage ack{};
  ack.type = RoomControlType::kJoinRoomAcceptedAck;
  ack.revision = applied_revision_;
  ack.request_id = msg.request_id;
  auto bytes = EncodeRoomControl(ack);
  if (hooks_.send_control && !host_uid_.empty()) {
    hooks_.send_control(host_uid_, bytes);
  }
  Log(ae::Format("ROOM_JOIN_ACCEPTED_ACK sent request_id={} rev={}",
                 msg.request_id, applied_revision_));

  if (hooks_.on_model_changed) {
    Trace("CLIENT_UI_PARTICIPANTS_DIRTY",
          ae::Format("request_id={}", msg.request_id));
    hooks_.on_model_changed();
  }
}

void RoomMembershipController::NotifyLocalJoinAppeared() {
  EnsureClientActiveIfJoined("notify_local_join", false);
}

void RoomMembershipController::OnControl(ae::Uid const& from,
                                         RoomControlMessage const& msg) {
  AssertBusinessThread("RoomMembershipController::OnControl");
  if (role_ == ChatRoomRole::kHost) {
    if (msg.type == RoomControlType::kJoinRoomRequest) {
      HostHandleJoinRequest(from, msg);
      return;
    }
    if (msg.type == RoomControlType::kJoinRoomAcceptedAck) {
      if (last_control_peer_ == from) {
        ClearLastControl();
      }
      Log(ae::Format("ROOM_JOIN_ACCEPTED_ACK rx from={} request_id={}",
                     FormatUid(from), msg.request_id));
      return;
    }
    Log(ae::Format("ROOM_CONTROL_IGNORE_HOST type={} from={}",
                   static_cast<int>(msg.type), FormatUid(from)));
    return;
  }

  if (host_uid_.empty() || from != host_uid_) {
    Log(ae::Format("ROOM_CONTROL_REJECT_NON_HOST from={}", FormatUid(from)));
    return;
  }
  switch (msg.type) {
    case RoomControlType::kJoinRoomAccepted:
      ClientOnJoinAccepted(msg);
      break;
    case RoomControlType::kParticipantsChanged:
      if (msg.revision > applied_revision_) {
        ClientApplyParticipants(msg);
        if (hooks_.on_model_changed) {
          hooks_.on_model_changed();
        }
        if (hooks_.on_ui_changed) {
          hooks_.on_ui_changed();
        }
      }
      break;
    case RoomControlType::kJoinRoomRejected:
      SetError(msg.display_name.empty() ? "membership_rejected"
                                        : msg.display_name);
      break;
    default:
      break;
  }
}

void RoomMembershipController::Tick(ae::TimePoint now) {
  AssertBusinessThread("RoomMembershipController::Tick");
  Trace("ROOM_TICK", ae::Format("status={}", static_cast<int>(ui_status_)));
  if (role_ == ChatRoomRole::kClient) {
    EnsureClientActiveIfJoined("tick", false);
  }
  if (last_control_payload_.empty() || !hooks_.send_control) {
    return;
  }
  if (now - last_control_sent_ < kRetryInterval) {
    return;
  }
  last_control_sent_ = now;
  if (last_control_peer_.empty()) {
    return;
  }
  Log(ae::Format("ROOM_CONTROL_RETRY peer={} bytes={}",
                 FormatUid(last_control_peer_), last_control_payload_.size()));
  hooks_.send_control(last_control_peer_, last_control_payload_);
}

}  // namespace apptraverse::chat
