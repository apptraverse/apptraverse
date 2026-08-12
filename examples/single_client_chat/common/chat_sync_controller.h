#ifndef APPTRAVERSE_EXAMPLES_CHAT_SYNC_CONTROLLER_H_
#define APPTRAVERSE_EXAMPLES_CHAT_SYNC_CONTROLLER_H_

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

#include "aether_p2p_outgoing_routing.h"
#include "model/chat.h"
#include "model/chat_peer_set.h"

namespace apptraverse::examples {

struct SyncRecoveryPolicy {
  std::chrono::milliseconds retry_interval{std::chrono::seconds{1}};
  std::chrono::milliseconds restream_after{std::chrono::seconds{3}};
  std::chrono::milliseconds replace_after{std::chrono::seconds{8}};
};

// Transport-independent operations. Concrete AetherP2pTransport is wired by
// the platform runtime; in-memory tests supply fakes.
struct SyncTransportOperations {
  std::function<void(ae::Uid const& peer)> ensure_outgoing;
  std::function<P2pOutgoingState(ae::Uid const& peer)> outgoing_state;
  std::function<void(ae::Uid const& peer)> restream_outgoing;
  std::function<void(ae::Uid const& peer)> replace_outgoing;
  std::function<void(ae::Uid const& peer, ae::ObjId packet_id,
                     SerializedSyncPacket const& bytes)>
      send;
};

// Runtime manager for SharedGraphSyncSession instances of one Chat.
// Not persisted; peer identity stays in ChatPeerSet only.
class ChatSyncController {
 public:
  using ChangedFunction = std::function<void()>;
  using LogFunction = std::function<void(std::string const&)>;

  ChatSyncController(SyncReplica replica, Chat::ptr chat,
                     ChatPeerSet::ptr peer_set, SyncTransportOperations ops,
                     SyncRecoveryPolicy policy, bool auto_accept_incoming,
                     ChangedFunction changed = {}, LogFunction log = {});

  void Start();
  SharedGraphSyncSession& AddPeer(ae::Uid const& remote_uid);
  void Receive(ae::Uid const& remote_uid, SerializedSyncPacket const& bytes);
  void Tick(ae::TimePoint now);

  std::size_t runtime_session_count() const { return sessions_.size(); }
  SharedGraphSyncSession* FindSession(ae::Uid const& remote_uid);
  SharedGraphSyncSession const* FindSession(ae::Uid const& remote_uid) const;

 private:
  struct RuntimeSession {
    ae::Uid remote_uid{};
    std::unique_ptr<SharedGraphSyncSession> session;
    bool last_initial_sync_complete{false};
    ae::TimePoint last_retry{};
    ae::TimePoint last_retry_gate_log{};
    std::size_t last_pending_count{0};

    // Runtime-only recovery state — never serialized.
    std::uint64_t last_ack_progress_revision{0};
    std::optional<ae::TimePoint> pending_since;
    ae::TimePoint last_restream_time{};
    ae::TimePoint last_replace_time{};
    bool restream_done_for_current_stall{false};
    bool replace_done_for_current_stall{false};
  };

  void Log(std::string const& line);
  void NotifyChanged();
  RuntimeSession& EnsureRuntimeSession(ae::Uid const& remote_uid,
                                       SyncSessionState::ptr state);
  void EmitInitialMarkers(RuntimeSession& runtime);
  void ResetRecoveryCycle(RuntimeSession& runtime, ae::TimePoint now);
  void DriveRecovery(RuntimeSession& runtime, ae::TimePoint now);

  SyncReplica replica_;
  Chat::ptr chat_;
  ChatPeerSet::ptr peer_set_;
  SyncTransportOperations ops_;
  SyncRecoveryPolicy policy_;
  bool auto_accept_incoming_{false};
  ChangedFunction changed_;
  LogFunction log_;
  std::vector<RuntimeSession> sessions_;
};

}  // namespace apptraverse::examples

#endif  // APPTRAVERSE_EXAMPLES_CHAT_SYNC_CONTROLLER_H_
