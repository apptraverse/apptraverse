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

#include "model/chat.h"
#include "chat_peer_schedule.h"
#include "chat_presence.h"
#include "model/chat_peer_set.h"
#include "sync_packet_write_gate.h"

namespace apptraverse::chat {

struct ChatSyncTiming {
  // How often DrivePending re-offers pending packets to the write gate.
  // Physical send limits are enforced by SyncPacketWriteGate, not this poll.
  std::chrono::milliseconds retry_interval{std::chrono::milliseconds{100}};
  // Minimum time between physical sends of the same pending packet_id.
  std::chrono::milliseconds packet_retry_interval{
      std::chrono::milliseconds{2000}};
  std::chrono::milliseconds heartbeat_interval{std::chrono::seconds{1}};
  std::chrono::milliseconds offline_timeout{std::chrono::seconds{5}};
};

// Runtime manager for SharedGraphSyncSession instances of one Chat.
// Not persisted; peer identity stays in ChatPeerSet only.
class ChatSyncController {
 public:
  using ChangedFunction = std::function<void()>;
  using LogFunction = std::function<void(std::string const&)>;
  using SendFunction = std::function<void(
      ae::Uid const& peer, ae::ObjId packet_id,
      SerializedSyncPacket const& bytes)>;
  using RawSendFunction =
      std::function<void(ae::Uid const& peer,
                         std::vector<std::uint8_t> const& bytes)>;
  // When set: unknown peers are auto-accepted only if this returns true.
  // When unset: legacy unconditional auto-accept.
  using IncomingPeerAuthorizeFunction =
      std::function<bool(ae::Uid const& remote_uid)>;
  // Rebuild the outbound transport session for one peer (drop + create).
  using ReconnectPeerFunction = std::function<void(ae::Uid const& remote_uid)>;

  ChatSyncController(SyncReplica replica, Chat::ptr chat,
                     ChatPeerSet::ptr peer_set, SendFunction send,
                     RawSendFunction raw_send, ChatSyncTiming timing,
                     ChangedFunction changed = {}, LogFunction log = {});

  void Start();
  void Stop();
  void SetIncomingPeerAuthorize(IncomingPeerAuthorizeFunction fn);
  // Inbound traffic can prove the remote restarted while our own outbound
  // session still points at its previous process. Without this the controller
  // can only re-offer packets on that stale path.
  void SetReconnectPeer(ReconnectPeerFunction fn);
  // Business-layer schedule query. Production injects the Aether adapter;
  // headless tests inject a fake. Unset: no schedule gating.
  void SetQueryPeerSchedule(QueryPeerScheduleFunction fn);
  void SetQueryPeerOnlineSchedule(QueryPeerScheduleFunction fn) {
    SetQueryPeerSchedule(std::move(fn));
  }
  SharedGraphSyncSession& AddPeer(ae::Uid const& remote_uid);
  // Notify sessions that a local Event was committed so they can publish
  // immediately without waiting for Tick/Poll.
  void LocalEventCommitted(Node::ptr node, EventRecord const& record);
  void Receive(ae::Uid const& remote_uid,
               std::vector<std::uint8_t> const& bytes);
  void Tick(ae::TimePoint now);

  // Transport replaced a stale P2P session (peer process restart). Clear the
  // write-gate cooldown and re-offer pending packets at most once per
  // transport generation.
  void NotifyTransportSessionReady(ae::Uid const& remote_uid,
                                   std::uint64_t transport_generation);

  std::size_t runtime_session_count() const { return sessions_.size(); }
  SharedGraphSyncSession* FindSession(ae::Uid const& remote_uid);
  SharedGraphSyncSession const* FindSession(ae::Uid const& remote_uid) const;
  bool IsPeerOnline(ae::Uid const& remote_uid) const;
  PeerReachability GetPeerReachability(ae::Uid const& remote_uid) const;
  bool IsPeerOfflineMissedVisit(ae::Uid const& remote_uid) const;
  bool IsPeerOfflineNoFuturePing(ae::Uid const& remote_uid) const;
  bool ShowOfflinePingMarker(ae::Uid const& remote_uid) const;

  // Diagnostics / unit tests: runtime gate state (not persisted).
  std::size_t write_gate_size(ae::Uid const& remote_uid) const;
  std::uint64_t physical_attempt_count(ae::Uid const& remote_uid,
                                       ae::ObjId packet_id) const;
  bool write_gate_has(ae::Uid const& remote_uid, ae::ObjId packet_id) const;

 private:
  struct RuntimeSession {
    ae::Uid remote_uid{};
    std::unique_ptr<SharedGraphSyncSession> session;
    SyncPacketWriteGate write_gate;
    bool last_initial_sync_complete{false};
    ae::TimePoint last_retry{};
    std::size_t last_pending_count{0};
    bool ever_seen_online{false};
    bool currently_online{false};
    std::optional<ae::TimePoint> last_seen;
    ae::TimePoint last_heartbeat_sent{};
    // Last transport generation that received SYNC_RECONNECT_FLUSH.
    std::uint64_t last_flushed_transport_generation{0};
    // New transport generation, presence rejoin, peer activity and stale peer
    // are alternative detectors of one event: "the remote is reachable again".
    // They share a single immediate re-offer; afterwards the write gate owns
    // the retry cadence. Reset on peer offline and on pending progress.
    bool recovery_flush_done{false};
    // One outbound-path rebuild per "remote came back" event; cleared when a
    // fresh transport generation arrives or pending drains.
    bool stale_path_reconnect_requested{false};
    // Set on presence-offline. Stale-path rebuild is allowed only after this,
    // so the first inbound of a fresh room does not tear down a live session.
    bool rebuild_after_offline{false};
    PeerReachability reachability{PeerReachability::kUnknown};
    std::int64_t last_ping_server_ms{0};
    std::int64_t next_ping_delta_ms{0};
    std::optional<std::chrono::steady_clock::time_point> local_deadline;
    std::optional<ae::TimePoint> tick_deadline;
    bool had_valid_uap{false};
    bool schedule_query_in_flight{false};
    ae::TimePoint last_schedule_query{};
    bool offline_marker_on{false};
  };

  struct PendingAutoAccept {
    ae::Uid remote_uid{};
    std::vector<std::vector<std::uint8_t>> packets;
    // First Tick only arms; AddPeer runs on a later Tick so it cannot
    // re-enter Aether while the receive/Update that queued us is unwinding.
    bool armed{false};
  };

  void Log(std::string const& line);
  void NotifyChanged();
  RuntimeSession* FindRuntime(ae::Uid const& remote_uid);
  RuntimeSession const* FindRuntime(ae::Uid const& remote_uid) const;
  RuntimeSession& EnsureRuntimeSession(ae::Uid const& remote_uid,
                                       SyncSessionState::ptr state);
  void EmitInitialMarkers(RuntimeSession& runtime);
  void DrivePending(RuntimeSession& runtime, ae::TimePoint now);
  void OfferPhysicalSend(RuntimeSession& runtime, ae::ObjId packet_id,
                         SerializedSyncPacket bytes, ae::TimePoint now);
  bool IsPersistentPending(RuntimeSession const& runtime,
                           ae::ObjId packet_id) const;
  void PruneWriteGate(RuntimeSession& runtime);
  void SendPresence(RuntimeSession& runtime, ChatPresenceMessage message,
                    ae::TimePoint now);
  void ApplyOnlineTransition(RuntimeSession& runtime);
  void ApplyOfflineTransition(RuntimeSession& runtime, char const* reason);
  // Single immediate re-offer of gated pending packets after the remote became
  // reachable again. Returns false when the token was already spent.
  bool TryRecoveryFlush(RuntimeSession& runtime, char const* reason,
                        std::uint64_t transport_generation);
  void FlushPendingImmediate(RuntimeSession& runtime,
                             std::uint64_t transport_generation,
                             char const* reason);
  void FlushPendingOnPresenceRejoin(RuntimeSession& runtime);
  void FlushPendingOnPeerActivity(RuntimeSession& runtime);
  // Rebuilds the outbound session when inbound proof of the remote's return
  // arrives while pending packets are still bound to the pre-restart path.
  bool RequestStalePathReconnect(RuntimeSession& runtime, char const* reason);
  void DrivePresence(RuntimeSession& runtime, ae::TimePoint now);
  void RequestPeerSchedule(RuntimeSession& runtime);
  void OnPeerScheduleResult(RuntimeSession& runtime,
                            std::optional<PeerScheduleSnapshot> result);
  bool PayloadRetriesAllowed(RuntimeSession const& runtime) const;
  void MaybeHandleScheduleDeadline(RuntimeSession& runtime, ae::TimePoint now);
  void MaybeRetryScheduleQuery(RuntimeSession& runtime, ae::TimePoint now);
  void RearmFromPeerOnlineNotify(RuntimeSession& runtime);
  void ImmediatePayloadRetry(RuntimeSession& runtime, ae::TimePoint now);
  void ApplyTickDeadline(RuntimeSession& runtime,
                         PeerScheduleSnapshot const& snap, ae::TimePoint now);
  void EnterOfflineMissedPing(RuntimeSession& runtime);
  void EnterOfflineNoFuturePing(RuntimeSession& runtime);
  void ClearOfflinePingMarker(RuntimeSession& runtime);
  void QueueAutoAccept(ae::Uid const& remote_uid,
                       std::vector<std::uint8_t> const& bytes);
  void DrainPendingAutoAccept();
  // Known-peer receive path; never creates peers or re-enters auto-accept.
  void ReceiveKnown(ae::Uid const& remote_uid,
                    std::vector<std::uint8_t> const& bytes);

  SyncReplica replica_;
  Chat::ptr chat_;
  ChatPeerSet::ptr peer_set_;
  SendFunction send_;
  RawSendFunction raw_send_;
  ChatSyncTiming timing_;
  ChangedFunction changed_;
  LogFunction log_;
  IncomingPeerAuthorizeFunction incoming_peer_authorize_;
  ReconnectPeerFunction reconnect_peer_;
  QueryPeerScheduleFunction query_peer_schedule_;
  std::vector<RuntimeSession> sessions_;
  // Runtime-only; packets arriving for unknown peers before AddPeer completes.
  std::vector<PendingAutoAccept> pending_auto_accept_;
  std::optional<ae::TimePoint> last_tick_now_;
};

}  // namespace apptraverse::chat

#endif  // APPTRAVERSE_CHAT_SYNC_CONTROLLER_H_
