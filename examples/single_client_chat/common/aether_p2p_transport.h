#ifndef APPTRAVERSE_EXAMPLES_AETHER_P2P_TRANSPORT_H_
#define APPTRAVERSE_EXAMPLES_AETHER_P2P_TRANSPORT_H_

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "aether/all.h"
#include "aether/ae_actions/query_peer_receive_schedule.h"
#include "aether/receive_schedule.h"

#include "aether_p2p_framing.h"
#include "chat_peer_schedule.h"

namespace apptraverse::examples {

inline constexpr char kP2pPingPayload[] = "APPTRAVERSE_P2P_PING_V1";
inline constexpr char kP2pPongPayload[] = "APPTRAVERSE_P2P_PONG_V1";

enum class ScheduleQueryPhase : std::uint8_t {
  Idle = 0,
  LocalInFlight = 1,
  RemoteInFlight = 2,
};

// Opaque framed byte transport over raw Aether P2pStream.
// Reliability (ack / retry / duplicate suppression) belongs to the
// synchronization layer — this transport does not provide it.
// Peer Aether UID is transport context only and is never placed in the payload.
//
// One PeerSession (one P2pStream) per remote UID in a process.
// Each (peer, generation) pair identifies a session lifetime; stream callbacks
// capture generation and ignore stale deliveries after replacement.
// Sends before the stream is writable are queued in pending_frames and flushed
// on SessionReady (first writable). Frame ids are monotonic per transport.
//
// Presence: at most one Client::QueryPeerReceiveSchedule at a time (local then
// optional remote). Never start the next query inside the result callback.
class AetherP2pTransport {
 public:
  using ReceiveHandler = std::function<void(
      ae::Uid const& peer, std::vector<std::uint8_t> const& payload)>;
  using LogHandler = std::function<void(std::string line)>;
  using PreWriteHandler = std::function<void(
      ae::Uid const& peer, std::size_t frame_bytes)>;
  using SessionReadyHandler = std::function<void(
      ae::Uid const& peer, char const* source, std::uint64_t generation)>;
  using ScheduleResultHandler = std::function<void(
      ae::Uid peer, bool is_local,
      std::optional<chat::PeerScheduleSnapshot> result)>;

  AetherP2pTransport() = default;
  AetherP2pTransport(AetherP2pTransport const&) = delete;
  AetherP2pTransport& operator=(AetherP2pTransport const&) = delete;
  ~AetherP2pTransport();

  void Start(ae::AetherApp& aether_app, ae::Client::ptr local_client);
  // Unsubscribe and destroy all sessions before AetherApp teardown.
  void Stop();

  void Connect(ae::Uid const& remote_uid);
  void Reconnect(ae::Uid const& remote_uid);
  void DropSession(ae::Uid const& peer);

  void Send(ae::Uid const& remote_uid, std::uint8_t const* bytes,
            std::size_t size);
  void Send(ae::Uid const& remote_uid,
            std::vector<std::uint8_t> const& bytes);
  void SendText(ae::Uid const& remote_uid, std::string_view text);

  void SetReceiveHandler(ReceiveHandler handler);
  void SetLogHandler(LogHandler handler);
  void SetPreWriteHandler(PreWriteHandler handler);
  void SetSessionReadyHandler(SessionReadyHandler handler);

  void SetPresenceUids(ae::Uid local_uid, ae::Uid remote_uid);
  void SetScheduleResultHandler(ScheduleResultHandler handler);
  void SetWakeNetwork(std::function<void()> wake);
  // Call each network loop iteration; starts local query when Idle + due.
  void PollPresence(ae::TimePoint now);
  // Call from network queue AFTER schedule result callback returned.
  void ContinuePresenceCycle();
  ScheduleQueryPhase schedule_query_phase() const {
    return schedule_query_phase_;
  }

  // Ad-hoc path: rejects if a presence query is already in flight.
  void QueryPeerReceiveSchedule(ae::Uid const& peer,
                                chat::PeerScheduleQueryCallback cb);
  void QueryPeerPingSchedule(ae::Uid const& peer,
                             chat::PeerScheduleQueryCallback cb) {
    QueryPeerReceiveSchedule(peer, std::move(cb));
  }
  void QueryPeerOnlineSchedule(ae::Uid const& peer,
                               chat::PeerScheduleQueryCallback cb) {
    QueryPeerReceiveSchedule(peer, std::move(cb));
  }
  void AnnounceNextPingUnknown();
  bool BeginPrepareForShutdown();
  bool PollPrepareForShutdown();
  bool prepare_for_shutdown_done() const noexcept {
    return prepare_for_shutdown_done_;
  }

  std::uint64_t session_generation(ae::Uid const& peer) const;
  std::size_t live_session_count(ae::Uid const& peer) const;
  // 0 or 1 — Client keeps a single QueryPeerReceiveSchedule slot.
  std::size_t active_schedule_query_count() const;
  // Safe to call from the network loop: flush pending frames when writable.
  void Poll();

 private:
  struct PendingFrame {
    std::uint64_t id{0};
    std::vector<std::uint8_t> bytes;
    // Trace-only payload classification (join_request / join_accepted / ...).
    std::string kind;
  };

  struct PeerSession {
    ae::Uid remote_uid{};
    std::uint64_t generation{0};
    std::shared_ptr<ae::P2pStream> stream;
    ae::Subscription data_sub;
    ae::Subscription link_sub;
    std::string source;
    AetherP2pFrameDecoder decoder;
    // A freshly constructed P2pStream buffers writes until its cloud send path
    // is linked. Session-ready is announced only once it can actually write.
    bool announced_ready{false};
    bool session_ready_flushed{false};
    std::deque<PendingFrame> pending_frames;
  };

  void NotifySessionReadyWhenWritable(PeerSession& session);
  void FlushPendingFrames(PeerSession& session, bool from_session_ready);
  bool StreamWritable(PeerSession const& session) const;
  std::uint64_t AllocFrameId();

  PeerSession* FindSession(ae::Uid const& peer);
  PeerSession* FindSession(ae::Uid const& peer, std::uint64_t generation);
  PeerSession const* FindSession(ae::Uid const& peer) const;
  PeerSession* CreateSession(ae::Uid const& peer, ae::P2pPortHandle handle,
                             char const* source,
                             std::deque<PendingFrame> preserved_pending = {});
  PeerSession* GetOrCreateSession(ae::Uid const& peer);
  std::deque<PendingFrame> TakePendingFrames(PeerSession& session);
  void DestroySessionLocked(PeerSession& session, char const* reason,
                            bool clear_pending);

  void AttachIncoming(ae::P2pPortHandle handle);

  void OnRawStreamData(ae::Uid peer, std::uint64_t generation,
                       ae::DataBuffer const& data);
  void EmitPayload(ae::Uid const& peer,
                   std::vector<std::uint8_t> const& payload);
  void Log(std::string line) const;

  void StartLocalQuery();
  void StartRemoteQuery();
  void SubscribeScheduleResult(ae::Uid peer, bool is_local);

  static std::string UidKey(ae::Uid const& uid);

  ae::AetherApp* aether_app_{nullptr};
  ae::Client::ptr local_client_;
  ReceiveHandler on_receive_;
  LogHandler on_log_;
  PreWriteHandler on_pre_write_;
  SessionReadyHandler on_session_ready_;
  ScheduleResultHandler on_schedule_result_;
  std::function<void()> wake_network_;
  ae::Subscription new_port_sub_;
  std::vector<std::unique_ptr<PeerSession>> sessions_;
  // Monotonic generation per peer (survives Drop); bumped on each Create.
  std::unordered_map<std::string, std::uint64_t> next_generation_;
  // Prevents overlapping Reconnect creating duplicate Connect races.
  std::unordered_set<std::string> reconnect_in_flight_;
  ScheduleQueryPhase schedule_query_phase_{ScheduleQueryPhase::Idle};
  ae::Uid local_uid_{};
  ae::Uid remote_uid_{};
  ae::TimePoint next_presence_cycle_{};
  ae::Subscription schedule_result_sub_;
  bool schedule_query_completed_{false};
  std::uint64_t next_frame_id_{1};
  std::unordered_set<std::uint64_t> flushed_frame_ids_;
  bool stopped_{false};
  bool prepare_for_shutdown_done_{true};
};

bool TryHandleP2pProbePayload(
    AetherP2pTransport& transport, ae::Uid const& peer,
    std::vector<std::uint8_t> const& payload,
    std::function<void(std::string const&)> const& log_line,
    std::function<void()> const& on_pong_received = {});

void AttachPingPongProbe(AetherP2pTransport& transport,
                         std::function<void(std::string const&)> log_line,
                         std::function<void()> on_pong_received = {});

void SendP2pPing(AetherP2pTransport& transport, ae::Uid const& peer,
                 std::function<void(std::string const&)> log_line);

}  // namespace apptraverse::examples

#endif  // APPTRAVERSE_EXAMPLES_AETHER_P2P_TRANSPORT_H_
