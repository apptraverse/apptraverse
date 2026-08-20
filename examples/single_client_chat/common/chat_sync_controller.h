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

  ChatSyncController(SyncReplica replica, Chat::ptr chat,
                     ChatPeerSet::ptr peer_set, SendFunction send,
                     RawSendFunction raw_send, ChatSyncTiming timing,
                     ChangedFunction changed = {}, LogFunction log = {});

  void Start();
  void Stop();
  SharedGraphSyncSession& AddPeer(ae::Uid const& remote_uid);
  void Receive(ae::Uid const& remote_uid,
               std::vector<std::uint8_t> const& bytes);
  void Tick(ae::TimePoint now);

  std::size_t runtime_session_count() const { return sessions_.size(); }
  SharedGraphSyncSession* FindSession(ae::Uid const& remote_uid);
  SharedGraphSyncSession const* FindSession(ae::Uid const& remote_uid) const;
  bool IsPeerOnline(ae::Uid const& remote_uid) const;

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
  void DrivePresence(RuntimeSession& runtime, ae::TimePoint now);
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
  std::vector<RuntimeSession> sessions_;
  // Runtime-only; packets arriving for unknown peers before AddPeer completes.
  std::vector<PendingAutoAccept> pending_auto_accept_;
  std::optional<ae::TimePoint> last_tick_now_;
};

}  // namespace apptraverse::chat

#endif  // APPTRAVERSE_CHAT_SYNC_CONTROLLER_H_
