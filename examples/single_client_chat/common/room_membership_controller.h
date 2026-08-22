#ifndef APPTRAVERSE_ROOM_MEMBERSHIP_CONTROLLER_H_
#define APPTRAVERSE_ROOM_MEMBERSHIP_CONTROLLER_H_

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "aether/clock.h"
#include "aether/types/uid.h"

#include "model/chat_room_local_state.h"
#include "room_control.h"

namespace apptraverse::chat {

enum class RoomUiStatus : std::uint8_t {
  kDisconnected = 0,
  kConnecting,
  kWaitingForPrepare,
  kWaitingForSnapshot,
  kWaitingForActivate,
  kWaitingForOwnJoin,
  kActive,
  kError,
};

struct RoomLocalJoinIdentity {
  bool obj_id_match{false};
  bool name_fallback{false};
  std::uint32_t local_client_obj_id{0};
  std::uint32_t join_client_obj_id{0};
};

struct RoomMembershipHooks {
  std::function<void(ae::Uid const&, std::vector<std::uint8_t> const&)>
      send_control;
  std::function<void(ae::Uid const&)> connect_peer;
  std::function<void(ae::Uid const&)> add_chat_peer;
  // Host: create Client(with_id)+JoinClientEvent once. Returns false if Join
  // already existed.
  std::function<bool(ae::Uid const& uid, std::uint32_t client_obj_id,
                     std::string const& name)>
      ensure_host_join;
  // ObjId-only Join match (activation). Name fallback must not return true.
  std::function<bool()> has_local_join;
  // Optional identity probe for JOIN_IDENTITY_* transition traces.
  std::function<RoomLocalJoinIdentity()> probe_local_join;
  std::function<void()> on_ui_changed;
  std::function<void()> on_model_changed;
  std::function<void(std::string const&)> log;
};

class RoomMembershipController {
 public:
  RoomMembershipController(ChatRoomRole role, ae::Uid local_uid,
                           std::uint32_t local_client_obj_id,
                           std::string local_name, ChatRoomLocalState::ptr state,
                           RoomMembershipHooks hooks);

  RoomUiStatus ui_status() const { return ui_status_; }
  std::string const& error() const { return error_; }
  ae::Uid const& host_uid() const { return host_uid_; }
  std::uint64_t applied_revision() const { return applied_revision_; }
  bool CanSendChat() const { return ui_status_ == RoomUiStatus::kActive; }
  bool IsAuthorizedSyncPeer(ae::Uid const& uid) const;
  std::vector<RoomParticipantDesc> ActiveParticipants() const;

  // Host: seed self as revision 1 participant after local Join exists.
  void HostBootstrap();

  // Client: begin connection to host.
  void ClientConnect(ae::Uid host_uid);
  // Active Client: idempotent Hello + transport connect after Host restart.
  void ClientNudgeReconnect();

  void OnControl(ae::Uid const& from, RoomControlMessage const& msg);
  void Tick(ae::TimePoint now);
  void NotifyLocalJoinAppeared();

 private:
  struct PendingHello {
    ae::Uid uid{};
    std::uint32_t client_obj_id{0};
    std::string name;
  };

  enum class HostPhase : std::uint8_t {
    kIdle = 0,
    kWaitPrepared,
    kWaitApplied,
    kWaitActivated,
  };

  void SetStatus(RoomUiStatus status);
  // If the local Join is already in the Chat journal, promote Client to Active
  // (or re-assert Send enabled after reconnect without a status change).
  void EnsureClientActiveIfJoined(char const* reason,
                                  bool refresh_active_ui = false);
  void SetError(std::string code);
  void PersistState();
  void Log(std::string const& line);
  RoomParticipantDesc MakeLocalDesc() const;
  std::vector<RoomParticipantDesc> CandidateWith(
      PendingHello const& hello) const;
  bool FindActive(ae::Uid const& uid, std::uint32_t* client_obj_id,
                  std::string* name) const;
  void HostReject(ae::Uid const& to, std::string reason);
  void HostStartNext();
  void HostSend(RoomControlType type, std::uint64_t revision,
                std::vector<RoomParticipantDesc> const& parts,
                ae::Uid const& to);
  void HostBroadcast(RoomControlType type, std::uint64_t revision,
                     std::vector<RoomParticipantDesc> const& parts);
  void HostOnPrepared(ae::Uid const& from, std::uint64_t revision);
  void HostOnApplied(ae::Uid const& from, std::uint64_t revision);
  void HostOnActivated(ae::Uid const& from, std::uint64_t revision);
  void HostFinishActivation();
  void HostHandleHello(ae::Uid const& from, RoomControlMessage const& msg);
  void HostHandleReconnect(PendingHello const& hello);
  void ClientSend(RoomControlType type, std::uint64_t revision);
  void ClientApplySnapshot(RoomControlMessage const& msg);
  void ClientActivate(std::uint64_t revision);

  // Product scope: Host + one Client (revision 1 → 2). Second distinct client
  // behavior is undefined in this version. hello_queue_ supports reconnect of
  // the accepted Client while an activation is in flight.
  ChatRoomRole role_;
  ae::Uid local_uid_{};
  std::uint32_t local_client_obj_id_{0};
  std::string local_name_;
  ChatRoomLocalState::ptr state_;
  RoomMembershipHooks hooks_;

  RoomUiStatus ui_status_{RoomUiStatus::kDisconnected};
  std::string error_;
  ae::Uid host_uid_{};
  std::uint64_t applied_revision_{0};
  std::vector<RoomParticipantDesc> authorized_;

  // Host activation
  HostPhase host_phase_{HostPhase::kIdle};
  std::deque<PendingHello> hello_queue_;
  PendingHello current_hello_{};
  std::uint64_t candidate_revision_{0};
  std::vector<RoomParticipantDesc> candidate_list_;
  std::unordered_set<std::string> waiting_acks_;  // uid strings
  std::vector<std::uint8_t> last_control_payload_;
  ae::Uid last_control_peer_{};
  bool last_control_broadcast_{false};
  ae::TimePoint last_control_sent_{};
  ae::TimePoint activation_started_{};
  bool activation_is_reconnect_{false};

  static std::string UidKey(ae::Uid const& uid);
};

}  // namespace apptraverse::chat

#endif  // APPTRAVERSE_ROOM_MEMBERSHIP_CONTROLLER_H_
