#ifndef APPTRAVERSE_CHAT_SYNC_CONTROLLER_H_
#define APPTRAVERSE_CHAT_SYNC_CONTROLLER_H_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "aether/clock.h"
#include "aether/obj/obj_id.h"
#include "aether/types/uid.h"

#include "apptraverse/object_graph_copy.h"
#include "apptraverse/shared_graph_sync_session.h"
#include "apptraverse/sync_packet.h"

#include "chat_peer_schedule.h"
#include "model/chat.h"
#include "model/chat_peer_set.h"

namespace apptraverse::chat {

struct ChatSyncTiming {
  // Packet ACK retry cadence while peer is Online (presence cycle).
  std::chrono::milliseconds retry_interval{std::chrono::seconds{3}};
};

// Runtime manager for SharedGraphSyncSession instances of one Chat.
// Not persisted; peer identity stays in ChatPeerSet only.
// Presence is derived from Aether receive-schedule queries only (no app
// heartbeat protocol).
class ChatSyncController {
 public:
  using ChangedFunction = std::function<void()>;
  using LogFunction = std::function<void(std::string const&)>;
  using SendFunction = std::function<void(ae::Uid const& peer, ae::ObjId packet_id,
                                          SerializedSyncPacket const& bytes)>;
  using RawSendFunction =
      std::function<void(ae::Uid const& peer,
                         std::vector<std::uint8_t> const& bytes)>;
  // When set: unknown peers are accepted only if this returns true.
  // When unset: legacy unconditional accept.
  using IncomingPeerAuthorizeFunction =
      std::function<bool(ae::Uid const& remote_uid)>;

  ChatSyncController(SyncReplica replica, Chat::ptr chat,
                     ChatPeerSet::ptr peer_set, SendFunction send,
                     RawSendFunction raw_send, ChatSyncTiming timing,
                     ChangedFunction changed = {}, LogFunction log = {});

  void Start();
  void Stop();
  void SetIncomingPeerAuthorize(IncomingPeerAuthorizeFunction fn);
  void SetQueryPeerSchedule(QueryPeerScheduleFunction fn);
  void SetLocalUid(ae::Uid local_uid) { local_uid_ = std::move(local_uid); }
  // Host star relay: after applying a remote event, Poll/publish to other peers.
  void SetHostEventRelay(bool enabled) { host_event_relay_ = enabled; }
  bool host_event_relay() const { return host_event_relay_; }
  SharedGraphSyncSession& AddPeer(ae::Uid const& remote_uid);
  // Notify sessions that a local Event was committed so they can publish
  // immediately without waiting for Tick/Poll.
  void LocalEventCommitted(Node::ptr node, EventRecord const& record);
  void Receive(ae::Uid const& remote_uid,
               std::vector<std::uint8_t> const& bytes);
  // After room authorization changes: AddPeer + ReceiveKnown for stashed UIDs.
  void FlushUnauthorizedIfAuthorized();
  void Tick(ae::TimePoint now);

  // Network-owned presence cycle delivers results here (business thread).
  void OnLocalScheduleResult(std::optional<PeerScheduleSnapshot> result);
  void OnPeerScheduleResult(ae::Uid const& peer,
                            std::optional<PeerScheduleSnapshot> result);

  std::size_t runtime_session_count() const { return sessions_.size(); }
  SharedGraphSyncSession* FindSession(ae::Uid const& remote_uid);
  SharedGraphSyncSession const* FindSession(ae::Uid const& remote_uid) const;
  bool IsPeerOnline(ae::Uid const& remote_uid) const;
  PeerPresenceStatus GetPeerPresence(ae::Uid const& remote_uid) const;
  LocalPresenceStatus GetLocalPresence() const { return local_presence_; }

 private:
  struct RuntimeSession {
    ae::Uid remote_uid{};
    std::unique_ptr<SharedGraphSyncSession> session;
    bool last_initial_sync_complete{false};
    ae::TimePoint last_retry{};
    std::size_t last_pending_count{0};
    PeerPresenceStatus presence{PeerPresenceStatus::kUnknown};
    std::optional<PeerScheduleSnapshot> last_schedule;
    bool schedule_query_in_flight{false};
    bool retry_skip_logged{false};
    bool pending_flush_done{false};
  };

  // Raw packets for peers not yet authorized; flushed on same business wake
  // once IncomingPeerAuthorize returns true (no two-tick arm/drain).
  struct PendingUnauthorized {
    ae::Uid remote_uid{};
    std::vector<std::vector<std::uint8_t>> packets;
  };

  void Log(std::string const& line);
  void NotifyChanged();
  RuntimeSession* FindRuntime(ae::Uid const& remote_uid);
  RuntimeSession const* FindRuntime(ae::Uid const& remote_uid) const;
  RuntimeSession& EnsureRuntimeSession(ae::Uid const& remote_uid,
                                       SyncSessionState::ptr state);
  void EmitInitialMarkers(RuntimeSession& runtime);
  void DrivePending(RuntimeSession& runtime, ae::TimePoint now);
  void SetPeerPresence(RuntimeSession& runtime, PeerPresenceStatus next,
                       char const* reason);
  void RefreshPresenceSchedules(ae::TimePoint now);
  void RequestPeerSchedule(RuntimeSession& runtime);
  void RequestLocalSchedule();
  void ApplyPeerScheduleResult(RuntimeSession& runtime,
                               std::optional<PeerScheduleSnapshot> result);
  void FlushPendingOnOnline(RuntimeSession& runtime, ae::TimePoint now);
  void StashUnauthorized(ae::Uid const& remote_uid,
                         std::vector<std::uint8_t> const& bytes);
  bool IsIncomingAuthorized(ae::Uid const& remote_uid) const;
  // Known-peer receive path; never creates peers.
  void ReceiveKnown(ae::Uid const& remote_uid,
                    std::vector<std::uint8_t> const& bytes);
  void ForwardAppliedEventsToOtherPeers(ae::Uid const& source_uid);

  SyncReplica replica_;
  Chat::ptr chat_;
  ChatPeerSet::ptr peer_set_;
  SendFunction send_;
  RawSendFunction raw_send_;
  ChatSyncTiming timing_;
  ChangedFunction changed_;
  LogFunction log_;
  IncomingPeerAuthorizeFunction incoming_peer_authorize_;
  QueryPeerScheduleFunction query_peer_schedule_;
  bool host_event_relay_{false};
  ae::Uid local_uid_{};
  LocalPresenceStatus local_presence_{LocalPresenceStatus::kConnecting};
  bool local_schedule_ever_ok_{false};
  std::optional<PeerScheduleSnapshot> local_schedule_;
  bool local_schedule_query_in_flight_{false};
  std::optional<ae::TimePoint> last_presence_refresh_;
  ae::TimePoint last_tick_now_{};
  std::vector<RuntimeSession> sessions_;
  std::vector<PendingUnauthorized> pending_unauthorized_;
};

}  // namespace apptraverse::chat

#endif  // APPTRAVERSE_CHAT_SYNC_CONTROLLER_H_
