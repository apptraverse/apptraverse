#ifndef APPTRAVERSE_ROOM_MEMBERSHIP_CONTROLLER_H_
#define APPTRAVERSE_ROOM_MEMBERSHIP_CONTROLLER_H_

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "aether/clock.h"
#include "aether/types/uid.h"

#include "model/chat_room_local_state.h"
#include "room_control.h"

namespace apptraverse::chat {

enum class RoomUiStatus : std::uint8_t {
  kDisconnected = 0,
  kConnecting,
  kSyncing,
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
  // Star topology: host → joining client only; client → host only.
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
  std::vector<RoomParticipantDesc> const& ActiveParticipants() const;

  // Host: seed self as revision 1 participant; status Active.
  void HostBootstrap();

  // Client: connect to host and send JoinRoomRequest.
  void ClientConnect(ae::Uid host_uid);

  // SessionReady: no last_control resend (FIFO flush only on transport).
  void OnTransportSessionReady(ae::Uid const& peer);

  void OnControl(ae::Uid const& from, RoomControlMessage const& msg);
  void Tick(ae::TimePoint now);
  void NotifyLocalJoinAppeared();

 private:
  void SetStatus(RoomUiStatus status);
  void EnsureClientActiveIfJoined(char const* reason,
                                  bool refresh_active_ui = false);
  void SetError(std::string code);
  void PersistState();
  void Log(std::string const& line);
  RoomParticipantDesc MakeLocalDesc() const;
  bool FindActive(ae::Uid const& uid, std::uint32_t* client_obj_id,
                  std::string* name) const;
  void HostReject(ae::Uid const& to, std::uint64_t request_id,
                  std::string reason);
  void HostHandleJoinRequest(ae::Uid const& from,
                             RoomControlMessage const& msg);
  void HostBroadcastParticipantsChanged(ae::Uid const& except);
  void ClientSendJoinRequest();
  void ClientApplyParticipants(RoomControlMessage const& msg);
  void ClientOnJoinAccepted(RoomControlMessage const& msg);
  void RememberLastControl(ae::Uid const& peer,
                           std::vector<std::uint8_t> bytes);
  void ClearLastControl();
  std::uint64_t NextRequestId();

  static std::string UidKey(ae::Uid const& uid);

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

  // Client JoinRoomRequest retry (lost packet only — not SessionReady resend).
  std::uint64_t next_request_id_{1};
  std::uint64_t pending_join_request_id_{0};
  std::vector<std::uint8_t> last_control_payload_;
  ae::Uid last_control_peer_{};
  ae::TimePoint last_control_sent_{};
};

}  // namespace apptraverse::chat

#endif  // APPTRAVERSE_ROOM_MEMBERSHIP_CONTROLLER_H_
