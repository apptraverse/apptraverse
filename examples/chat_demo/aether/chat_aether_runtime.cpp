#include "chat_aether_runtime.h"
#include "aether_stream_frame.h"
#include "join_delivery_trace.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "aether/adapters/ethernet.h"
#include "aether/client_messages/p2p_safe_message_stream.h"
#include "aether/config.h"
#include "aether/global_ids.h"
#include "aether/safe_stream/safe_stream_config.h"
#include "aether/tele_statistics.h"
#include "ae-numeric/percentile.h"

#include "apptraverse/directory_domain_storage.h"

namespace apptraverse::example::chat_demo {
namespace {

constexpr ae::SafeStreamConfig kChatSafeStreamConfig{
    .window_size = AE_SAFE_STREAM_CAPACITY / 2 - 1,
    .max_packet_size = AE_SAFE_STREAM_CAPACITY / 2 - 1,
    .max_repeat_count = 10,
    .wait_ack_timeout = std::chrono::seconds{5},
    .send_ack_timeout = std::chrono::seconds{0},
    .send_repeat_timeout = std::chrono::seconds{2},
};

// Initial Join ACK application payloads are small; journal Events are larger.
// Used to half-duplex the first post-ACK Event Write.
constexpr std::size_t kInitialAckAppPayloadMax = 64;
constexpr std::uint64_t kWriteHangRecoverMs = 3000;

class LocalConnectivityMonitor {
 public:
  void Configure(ae::Client::ptr client,
                 ChatAetherRuntime::LocalConnectivityCallback callback) {
    client_ = std::move(client);
    callback_ = std::move(callback);
    last_reported_.reset();
  }

  void Tick(ae::TimePoint now) {
    if (!client_ || !client_->connectivity_policy().is_valid() || !callback_) {
      return;
    }
    auto const& policy = client_->connectivity_policy().Load();
    auto const diag = policy->DiagnoseLocalPresence(now);
    if (last_reported_.has_value() && last_reported_->first == diag.has_schedule &&
        last_reported_->second == diag.any_online) {
      return;
    }
    last_reported_ = std::make_pair(diag.has_schedule, diag.any_online);
    callback_(diag.has_schedule, diag.any_online);
  }

 private:
  ae::Client::ptr client_;
  ChatAetherRuntime::LocalConnectivityCallback callback_;
  std::optional<std::pair<bool, bool>> last_reported_;
};


inline constexpr char const* kAetherParentUid =
    "3ac93165-3d37-4970-87a6-fa4ee27744e4";

inline std::uint64_t CurrentSteadyTimeMs() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

ae::AetherAppContext MakeAetherAppContext(
    std::shared_ptr<std::filesystem::path> const& state_dir_holder) {
  ae::AetherAppContext context{[state_dir_holder] {
    return std::unique_ptr<ae::IDomainStorage>{
        std::make_unique<apptraverse::DirectoryDomainStorage>(
            *state_dir_holder)};
  }};
#if AE_DISTILLATION
  context = std::move(context).AddAdapterFactory(
      [](ae::AetherAppContext const& app_context) {
        return ae::EthernetAdapter::ptr::Create(
            ae::CreateWith{app_context.domain()}.with_id(
                ae::GlobalId::kEthernetAdapter),
            app_context.aether(), app_context.poller(),
            app_context.dns_resolver());
      });
#endif
  return context;
}

}  // namespace

ChatAetherRuntime::ChatAetherRuntime() = default;

ChatAetherRuntime::~ChatAetherRuntime() {
  RequestStop();
  Join();
}

void ChatAetherRuntime::Start(Config config, LocalUidCallback on_uid,
                              ReadyCallback on_ready, FailedCallback on_failed,
                              FrameCallback on_frame,
                              PresenceCallback on_presence,
                              LocalConnectivityCallback on_local_connectivity,
                              ControlCallback on_control) {
  RequestStop();
  Join();

  if (config.heartbeat_period_ms < 250 ||
      config.offline_after_ms < config.heartbeat_period_ms * 2) {
    if (on_failed) {
      on_failed("Invalid heartbeat configuration");
    }
    return;
  }

  stop_ = false;
  thread_ = std::thread(
      &ChatAetherRuntime::ThreadMain, this, std::move(config), std::move(on_uid),
      std::move(on_ready), std::move(on_failed), std::move(on_frame),
      std::move(on_presence), std::move(on_local_connectivity),
      std::move(on_control));
}

void ChatAetherRuntime::OpenPeer(std::string peer_uid) {
  Enqueue(Command{.type = CommandType::kOpenPeer,
                  .peer_uid = std::move(peer_uid),
                  .bytes = {}});
}

void ChatAetherRuntime::Send(std::string peer_uid,
                             std::vector<std::uint8_t> bytes) {
  Enqueue(Command{.type = CommandType::kSend,
                  .peer_uid = std::move(peer_uid),
                  .bytes = std::move(bytes)});
}

void ChatAetherRuntime::SendControl(std::string peer_uid,
                                    std::vector<std::uint8_t> bytes) {
  Enqueue(Command{.type = CommandType::kSendControl,
                  .peer_uid = std::move(peer_uid),
                  .bytes = std::move(bytes)});
}

void ChatAetherRuntime::ClosePeer(std::string peer_uid) {
  Enqueue(Command{.type = CommandType::kClosePeer,
                  .peer_uid = std::move(peer_uid),
                  .bytes = {}});
}

void ChatAetherRuntime::RequestStop() { stop_ = true; }

void ChatAetherRuntime::Join() {
  if (thread_.joinable()) {
    thread_.join();
  }
}

void ChatAetherRuntime::SetFrameCallback(FrameCallback on_frame) {
  std::lock_guard<std::mutex> lock{callback_mu_};
  on_frame_ = std::move(on_frame);
}

void ChatAetherRuntime::Enqueue(Command command) {
  std::lock_guard<std::mutex> lock{command_mu_};
  commands_.push(std::move(command));
}

void ChatAetherRuntime::ThreadMain(Config config, LocalUidCallback on_uid,
                                  ReadyCallback on_ready,
                                  FailedCallback on_failed,
                                  FrameCallback on_frame,
                                  PresenceCallback on_presence,
                                  LocalConnectivityCallback on_local_connectivity,
                                  ControlCallback on_control) {
  {
    std::lock_guard<std::mutex> lock{callback_mu_};
    on_uid_ = std::move(on_uid);
    on_ready_ = std::move(on_ready);
    on_failed_ = std::move(on_failed);
    on_frame_ = std::move(on_frame);
    on_presence_ = std::move(on_presence);
    on_local_connectivity_ = std::move(on_local_connectivity);
    on_control_ = std::move(on_control);
  }

  try {
    std::filesystem::create_directories(config.state_dir);
    auto state_dir_holder =
        std::make_shared<std::filesystem::path>(config.state_dir);
    auto aether_app = ae::AetherApp::Construct(MakeAetherAppContext(state_dir_holder));

    ae::Client::ptr client;
    ae::Subscription inbound_sub;
    ae::Subscription select_sub;
    std::unordered_map<std::string, PeerState> peers;
    std::uint64_t next_nonce = 1;

    bool select_started = false;
    bool client_configured = false;
    bool select_failed = false;
    std::string select_error;
    LocalConnectivityMonitor local_connectivity;

    auto set_presence = [this, &peers](PeerState& peer, PeerPresence new_presence) {
      if (peer.reported_presence != new_presence) {
        peer.reported_presence = new_presence;
        PresenceCallback cb;
        {
          std::lock_guard<std::mutex> lock{callback_mu_};
          cb = on_presence_;
        }
        if (cb) {
          cb(peer.uid_text, new_presence);
        }
      }
    };

    // Assigned after flush_pending_out is defined (write completion drains queue).
    std::function<void(std::string const&, ae::WriteAction::Status)>
        on_write_status_fn;

    // Assigned after bind helpers are defined; Join ACK schedules one reset.
    // Never destroy P2pSafeStream from inside its own Write/out_data callback.
    auto schedule_safestream_reset_after_join =
        [](PeerState& peer, char const* why) {
          if (peer.post_join_safestream_reset ||
              peer.pending_join_safestream_reset || !peer.raw_p2p) {
            return;
          }
          peer.pending_join_safestream_reset = true;
          // Let SafeStream SendAck for the Join ACK finish before CreatePort.
          peer.join_reset_ready_ms =
              CurrentSteadyTimeMs() + 300;
          JoinTrace("SAFE_RESET_SCHED", "aether", why, 0, {}, peer.uid_text);
        };

    auto queue_pending = [](PeerState& peer, PendingOut item) {
      if (peer.write_in_flight && peer.in_flight_kind == item.kind &&
          peer.in_flight_bytes == item.bytes) {
        return;
      }
      if (!peer.pending_out.empty() &&
          peer.pending_out.back().kind == item.kind &&
          peer.pending_out.back().bytes == item.bytes) {
        return;
      }
      peer.pending_out.push_back(std::move(item));
    };

    auto write_encoded_frame =
        [&on_write_status_fn](PeerState& peer, AetherFrameKind kind,
                             std::vector<std::uint8_t>& payload_bytes,
                             std::string const& trace_stage,
                             std::string const& reason) {
          if (!peer.stream || peer.write_in_flight) {
            return false;
          }
          if (kind == AetherFrameKind::kApplication &&
              peer.pending_join_safestream_reset) {
            return false;
          }
          if (kind == AetherFrameKind::kApplication &&
              peer.defer_large_app_until_rx &&
              payload_bytes.size() > kInitialAckAppPayloadMax) {
            JoinTrace("APP_TX_DEFER", "aether", "half_duplex_wait_rx", 0, {},
                      peer.uid_text, {}, {}, payload_bytes.size(),
                      JoinTraceHash(payload_bytes), reason);
            return false;
          }
          auto const info = peer.stream->stream_info();
          if (info.rec_element_size == 0 || !info.is_writable) {
            JoinTrace("WRITE_WAIT_LINK", "aether", "rec_el_or_writable", 0, {},
                      peer.uid_text, {}, {}, payload_bytes.size(), 0,
                      info.rec_element_size == 0 ? "rec_el=0" : "not_writable");
            return false;
          }
          auto frame_bytes = EncodeAetherFrame(kind, payload_bytes);
          if (!trace_stage.empty()) {
            JoinTrace(trace_stage, "aether", "p2p_write", 0, {}, peer.uid_text,
                      {}, {}, frame_bytes.size(), JoinTraceHash(frame_bytes),
                      reason);
          }
          peer.write_in_flight = true;
          peer.write_started_ms = CurrentSteadyTimeMs();
          peer.in_flight_kind = kind;
          peer.in_flight_bytes = std::move(payload_bytes);
          ae::DataBuffer buffer{frame_bytes.begin(), frame_bytes.end()};
          auto& action = peer.stream->Write(std::move(buffer));
          peer.write_subs.push_back(action.status_event().Subscribe(
              [&on_write_status_fn, peer_uid = peer.uid_text](
                  ae::WriteAction::Status status) {
                on_write_status_fn(peer_uid, status);
              }));
          if (peer.write_subs.size() > 128) {
            peer.write_subs.erase(peer.write_subs.begin(),
                                  peer.write_subs.begin() + 64);
          }
          return true;
        };

    auto flush_pending_out =
        [&write_encoded_frame](PeerState& peer) {
          if (!peer.stream || peer.write_in_flight || peer.pending_out.empty()) {
            return;
          }
          auto& item = peer.pending_out.front();
          if (item.kind == AetherFrameKind::kApplication &&
              peer.defer_large_app_until_rx &&
              item.bytes.size() > kInitialAckAppPayloadMax) {
            return;
          }
          auto const info = peer.stream->stream_info();
          std::string reason = !info.is_writable
                                   ? "not_writable"
                                   : (info.link_state == ae::LinkState::kLinked
                                          ? "linked"
                                          : "not_linked");
          reason += " max_el=";
          reason += std::to_string(info.max_element_size);
          reason += " rec_el=";
          reason += std::to_string(info.rec_element_size);
          reason += " queued_flush";
          char const* stage = item.kind == AetherFrameKind::kControl
                                  ? "CTRL_TX"
                                  : (item.kind == AetherFrameKind::kApplication
                                         ? "APP_TX"
                                         : "");
          if (!write_encoded_frame(peer, item.kind, item.bytes,
                                   stage != nullptr ? stage : "", reason)) {
            return;
          }
          peer.pending_out.pop_front();
        };

    on_write_status_fn =
        [this, &peers, &set_presence, &config, &flush_pending_out,
         &schedule_safestream_reset_after_join](
            std::string const& peer_uid_text,
            ae::WriteAction::Status status) {
          auto it = peers.find(peer_uid_text);
          if (it == peers.end()) {
            return;
          }
          auto& peer = it->second;
          auto const finished_kind = peer.in_flight_kind;
          auto const finished_size = peer.in_flight_bytes.size();
          peer.write_in_flight = false;
          peer.write_started_ms = 0;
          peer.in_flight_bytes.clear();
          if (status == ae::WriteAction::Status::kFail) {
            JoinTrace("WRITE_FAIL", "aether", "status_fail", 0, {},
                      peer_uid_text, {}, {}, finished_size, 0,
                      "write_action_fail");
            peer.stream_linked = false;
            auto const now_ms = CurrentSteadyTimeMs();
            auto const last_active = (std::max)(peer.last_heartbeat_rx_ms,
                                                peer.last_rx_ms);
            if (now_ms - last_active >= config.offline_after_ms) {
              set_presence(peer, PeerPresence::kOffline);
            } else {
              set_presence(peer, PeerPresence::kConnecting);
            }
          } else if (status == ae::WriteAction::Status::kSuccess) {
            JoinTrace("WRITE_OK", "aether", "status_ok", 0, {}, peer_uid_text,
                      {}, {}, finished_size, 0, {});
            // Client initial Join ACK completed: reset SafeStream then wait
            // for Host's first Event before sending journal Events.
            if (finished_kind == AetherFrameKind::kApplication &&
                finished_size <= kInitialAckAppPayloadMax &&
                !peer.join_half_duplex_used) {
              // Do not rebuild SafeStream on the Client here: Host's post-ACK
              // reset uses send_reset against this live receive session. A
              // Client rebuild mid-Event drops Host's first journal Write.
              peer.join_half_duplex_used = true;
              peer.defer_large_app_until_rx = true;
              JoinTrace("HALF_DUPLEX_ARM", "aether", "defer_large_app", 0, {},
                        peer_uid_text, {}, {}, finished_size, 0,
                        "after_initial_ack_tx");
            }
          } else {
            JoinTrace("WRITE_STOP", "aether", "status_stop", 0, {},
                      peer_uid_text, {}, {}, finished_size, 0, "write_action_stop");
          }
          flush_pending_out(peer);
        };

    auto send_heartbeat = [&write_encoded_frame](PeerState& peer,
                                                 AetherFrameKind kind,
                                                 std::uint64_t nonce) {
      if (!peer.stream || peer.write_in_flight || !peer.pending_out.empty()) {
        // Do not contend with SafeStream application/control Writes.
        return;
      }
      auto nonce_payload = EncodeHeartbeatNonce(nonce);
      static_cast<void>(write_encoded_frame(peer, kind, nonce_payload, "",
                                            "heartbeat"));
    };

    auto on_stream_data = [this, &peers, &set_presence, &send_heartbeat,
                           &flush_pending_out,
                           &schedule_safestream_reset_after_join](
                              std::string const& peer_uid_text,
                              ae::DataBuffer const& data) {
      auto it = peers.find(peer_uid_text);
      if (it == peers.end()) {
        return;
      }
      auto& peer = it->second;
      std::vector<std::uint8_t> bytes(data.begin(), data.end());
      JoinTrace("RAW_RX", "aether", "before_outer_decode", 0, peer_uid_text, {},
                {}, {}, bytes.size(), JoinTraceHash(bytes));

      AetherFrameKind kind{};
      std::vector<std::uint8_t> payload;
      if (!DecodeAetherFrame(bytes, kind, payload)) {
        JoinTrace("RAW_RX_DECODE_FAIL", "aether", "bad_outer_frame", 0,
                  peer_uid_text, {}, {}, {}, bytes.size(), JoinTraceHash(bytes),
                  "bad outer frame");
        // Malformed frame: drop. Do not forward as application bytes.
        // It may update no application state.
        return;
      }

      auto const now_ms = CurrentSteadyTimeMs();
      peer.last_rx_ms = now_ms;

      if (kind == AetherFrameKind::kHeartbeatPing) {
        peer.last_heartbeat_rx_ms = now_ms;
        set_presence(peer, PeerPresence::kOnline);
        auto const nonce = DecodeHeartbeatNonce(payload);
        send_heartbeat(peer, AetherFrameKind::kHeartbeatPong, nonce);
        return;
      }

      if (kind == AetherFrameKind::kHeartbeatPong) {
        peer.last_heartbeat_rx_ms = now_ms;
        set_presence(peer, PeerPresence::kOnline);
        return;
      }

      if (kind == AetherFrameKind::kApplication) {
        set_presence(peer, PeerPresence::kOnline);
        JoinTrace("APP_RX", "aether", "kApplication", 0, peer_uid_text, {}, {},
                  {}, payload.size(), JoinTraceHash(payload));
        // Host receives Join ACK: open a fresh P2p SafeStream for Events.
        // Rebuilding the Join-port SafeStream mid-callback drops its SendAck
        // and leaves the Client's ACK Write hung; a new CreatePort avoids that.
        if (payload.size() <= kInitialAckAppPayloadMax) {
          schedule_safestream_reset_after_join(peer, "after_ack_rx");
        }
        if (peer.defer_large_app_until_rx &&
            payload.size() > kInitialAckAppPayloadMax) {
          peer.defer_large_app_until_rx = false;
          JoinTrace("HALF_DUPLEX_RELEASE", "aether", "got_peer_event", 0,
                    peer_uid_text, {}, {}, {}, payload.size(),
                    JoinTraceHash(payload), "resume_large_app_tx");
          flush_pending_out(peer);
        }

        FrameCallback cb;
        {
          std::lock_guard<std::mutex> lock{callback_mu_};
          cb = on_frame_;
        }
        if (cb) {
          cb(peer.uid_text, std::move(payload));
        }
        return;
      }

      if (kind == AetherFrameKind::kControl) {
        JoinTrace("CTRL_RX", "aether", "kControl", 0, peer_uid_text, {}, {}, {},
                  payload.size(), JoinTraceHash(payload));
        ControlCallback cb;
        {
          std::lock_guard<std::mutex> lock{callback_mu_};
          cb = on_control_;
        }
        if (cb) {
          cb(peer.uid_text, std::move(payload));
        }
      }
    };

    auto on_stream_update = [&peers, &set_presence, &flush_pending_out](
                                std::string const& peer_uid_text) {
      auto it = peers.find(peer_uid_text);
      if (it == peers.end() || !it->second.stream) {
        return;
      }
      auto& peer = it->second;
      auto const link_state = peer.stream->stream_info().link_state;
      if (link_state == ae::LinkState::kLinked) {
        peer.stream_linked = true;
        if (peer.reported_presence != PeerPresence::kOnline) {
          set_presence(peer, PeerPresence::kConnecting);
        }
        flush_pending_out(peer);
      } else if (link_state == ae::LinkState::kLinkError ||
                 link_state == ae::LinkState::kUnlinked) {
        peer.stream_linked = false;
        if (peer.reported_presence == PeerPresence::kOnline) {
          set_presence(peer, PeerPresence::kConnecting);
        }
      }
    };

    auto bind_peer_stream = [&peers, &set_presence, &flush_pending_out,
                             &on_stream_data, &on_stream_update, &aether_app](
                                std::string const& peer_uid_text, ae::Uid uid,
                                std::shared_ptr<ae::P2pStream> p2p_stream,
                                bool inbound) {
      (void)inbound;
      auto& peer = peers[peer_uid_text];
      peer.uid_text = peer_uid_text;
      peer.uid = uid;

      peer.data_sub.Reset();
      peer.update_sub.Reset();
      peer.write_subs.clear();
      peer.write_in_flight = false;
      peer.write_started_ms = 0;
      peer.in_flight_bytes.clear();
      peer.defer_large_app_until_rx = false;
      peer.join_half_duplex_used = false;
      peer.post_join_safestream_reset = false;
      peer.pending_join_safestream_reset = false;
      peer.join_reset_ready_ms = 0;

      // Fragment large frames (NodeState) across the ~1200-byte channel MTU.
      // Raw P2pStream WRITE_OK does not imply end-to-end delivery of oversized
      // cloud messages; P2pSafeStream is the native facility used by Aether
      // cloud examples for application payloads.
      peer.raw_p2p = std::move(p2p_stream);
      peer.stream = std::make_unique<ae::P2pSafeStream>(
          *aether_app, kChatSafeStreamConfig, peer.raw_p2p);
      peer.stream_linked =
          (peer.stream->stream_info().link_state == ae::LinkState::kLinked);

      if (peer.reported_presence != PeerPresence::kOnline) {
        set_presence(peer, PeerPresence::kConnecting);
      }
      if (peer.last_rx_ms == 0) {
        peer.last_rx_ms = CurrentSteadyTimeMs();
      }

      peer.data_sub = peer.stream->out_data_event().Subscribe(
          [&on_stream_data, peer_uid_text](ae::DataBuffer const& data) {
            on_stream_data(peer_uid_text, data);
          });

      peer.update_sub = peer.stream->stream_update_event().Subscribe(
          [&on_stream_update, peer_uid_text]() {
            on_stream_update(peer_uid_text);
          });

      flush_pending_out(peer);
    };

    // After Join ACK on Host, open a new outbound P2p port + SafeStream for
    // Event traffic. Rewrapping the Join receive port destroys the pending
    // SafeStream SendAck for that ACK and hangs the Client Write forever.
    auto apply_pending_safestream_resets =
        [&peers, &client, &aether_app, &on_stream_data, &on_stream_update,
         &flush_pending_out, &bind_peer_stream]() {
          for (auto& [peer_uid, peer] : peers) {
            (void)peer_uid;
            if (!peer.pending_join_safestream_reset || peer.write_in_flight ||
                peer.post_join_safestream_reset || !client) {
              continue;
            }
            if (CurrentSteadyTimeMs() < peer.join_reset_ready_ms) {
              continue;
            }
            peer.pending_join_safestream_reset = false;
            peer.post_join_safestream_reset = true;
            JoinTrace("SAFE_NEW_PORT", "aether", "post_join_events", 0, {},
                      peer.uid_text);
            auto handle =
                client->message_stream_manager().CreatePort(peer.uid);
            auto stream = std::make_shared<ae::P2pStream>(
                *aether_app, client.Load(), peer.uid, std::move(handle));
            // Preserve defer/half-duplex flags across rebind.
            auto const defer = peer.defer_large_app_until_rx;
            auto const half = peer.join_half_duplex_used;
            auto pending = std::move(peer.pending_out);
            bind_peer_stream(peer.uid_text, peer.uid, std::move(stream),
                             /*inbound=*/false);
            peer.defer_large_app_until_rx = defer;
            peer.join_half_duplex_used = half;
            peer.post_join_safestream_reset = true;
            peer.pending_out = std::move(pending);
            flush_pending_out(peer);
          }
        };

    auto open_peer_internal = [&peers, &client, &aether_app, &bind_peer_stream](
                                  std::string const& peer_uid_text) {
      if (peer_uid_text.empty()) {
        return;
      }
      auto uid = ae::Uid::FromString(peer_uid_text);
      if (uid.empty()) {
        return;
      }
      auto it = peers.find(peer_uid_text);
      if (it != peers.end() && it->second.stream) {
        return;
      }
      if (!client) {
        return;
      }

      auto handle = client->message_stream_manager().CreatePort(uid);
      auto stream = std::make_shared<ae::P2pStream>(
          *aether_app, client.Load(), uid, std::move(handle));
      bind_peer_stream(peer_uid_text, uid, std::move(stream),
                       /*inbound=*/false);
    };

    auto parent = ae::Uid::FromString(std::string{kAetherParentUid});

    while (!stop_ && !aether_app->IsExited()) {
      if (!select_started) {
        select_started = true;
        auto& select =
            aether_app->aether()->SelectClient(parent, config.client_name);
        select_sub = select.result_event().Subscribe(
            [&client, &client_configured, &select_failed,
             &select_error](ae::Result<ae::Client::ptr, int> const& res) {
              if (!res) {
                select_failed = true;
                select_error =
                    "SelectClient failed code=" + std::to_string(res.error());
                return;
              }
              client = res.value();
              client_configured = true;
            });
      }

      if (select_failed) {
        select_sub.Reset();
        FailedCallback failed_cb;
        {
          std::lock_guard<std::mutex> lock{callback_mu_};
          failed_cb = on_failed_;
        }
        if (failed_cb) {
          failed_cb(select_error);
        }
        break;
      }

      if (client_configured && !inbound_sub) {
        select_sub.Reset();
        auto const uid_text = ae::Format("{}", client->uid());

        static_cast<void>(client->cloud_connection());

        auto constexpr kPingInterval = std::chrono::seconds{1};
        auto constexpr kReceiveWindow = std::chrono::seconds{1};
        auto constexpr kOfflineTimeout = std::chrono::seconds{1};
        auto const conf =
            ae::RxTimingConf::Every(
                std::chrono::duration_cast<ae::Duration>(kPingInterval))
                .WithWindow(
                    std::chrono::duration_cast<ae::Duration>(kReceiveWindow));
        if (auto policy = client->connectivity_policy()) {
          policy->ResetRxTimings();
          policy->SetOfflineDetectionTimeout(
              std::chrono::duration_cast<ae::Duration>(kOfflineTimeout));
          policy->ConfigureRxTimings(ae::RequestPolicy::All{})
              .ForAllPriorities(conf);
          for (auto* server : client->cloud_connection().selected_servers()) {
            if (server != nullptr) {
              policy->ConfigureServerRxTiming(server->server_id(), conf,
                                              ae::Percentile::FromPercent(99.0));
            }
          }
        }

        aether_app->aether().Save();

        LocalUidCallback uid_cb;
        ReadyCallback ready_cb;
        {
          std::lock_guard<std::mutex> lock{callback_mu_};
          uid_cb = on_uid_;
          ready_cb = on_ready_;
        }

        if (uid_cb) {
          uid_cb(uid_text);
        }

        inbound_sub =
            client->message_stream_manager().new_port_event().Subscribe(
                [&bind_peer_stream, &client, &aether_app,
                 &peers](ae::P2pPortHandle handle) {
                  auto const uid = handle.destination();
                  auto const peer_uid_text = ae::Format("{}", uid);
                  // new_port_event fires only for a newly created receive port,
                  // immediately before Deliver. An existing Linked outbound
                  // stream does not subscribe that port; dropping the handle
                  // here loses Host→Client Accept/NodeState (and any later
                  // first packet on a replaced port). Always bind this handle.
                  if (auto it = peers.find(peer_uid_text);
                      it != peers.end() && it->second.stream) {
                    JoinTrace("NEW_PORT_REBIND", "aether",
                              "replace_existing_stream", 0, peer_uid_text);
                    // Preserve post-Join Event half-duplex across Host's
                    // fresh Event port.
                    auto& existing = it->second;
                    auto const defer = existing.defer_large_app_until_rx;
                    auto const half = existing.join_half_duplex_used;
                    auto pending = std::move(existing.pending_out);
                    auto stream = std::make_shared<ae::P2pStream>(
                        *aether_app, client.Load(), uid, std::move(handle));
                    bind_peer_stream(peer_uid_text, uid, std::move(stream),
                                     /*inbound=*/true);
                    existing.defer_large_app_until_rx = defer;
                    existing.join_half_duplex_used = half;
                    existing.pending_out = std::move(pending);
                    return;
                  } else {
                    JoinTrace("NEW_PORT_BIND", "aether", "inbound", 0,
                              peer_uid_text);
                  }
                  auto stream = std::make_shared<ae::P2pStream>(
                      *aether_app, client.Load(), uid, std::move(handle));
                  bind_peer_stream(peer_uid_text, uid, std::move(stream),
                                   /*inbound=*/true);
                });

        LocalConnectivityCallback local_cb;
        {
          std::lock_guard<std::mutex> lock{callback_mu_};
          local_cb = on_local_connectivity_;
        }
        local_connectivity.Configure(client, std::move(local_cb));

        if (ready_cb) {
          ready_cb();
        }
      }

      if (select_failed) {
        FailedCallback failed_cb;
        {
          std::lock_guard<std::mutex> lock{callback_mu_};
          failed_cb = on_failed_;
        }
        if (failed_cb) {
          failed_cb(select_error);
        }
        break;
      }

      // Process queued commands
      if (client_configured) {
        apply_pending_safestream_resets();
        std::queue<Command> local;
        {
          std::lock_guard<std::mutex> lock{command_mu_};
          local.swap(commands_);
        }
        while (!local.empty()) {
          auto cmd = std::move(local.front());
          local.pop();
          switch (cmd.type) {
            case CommandType::kOpenPeer:
              open_peer_internal(cmd.peer_uid);
              break;
            case CommandType::kSend:
            case CommandType::kSendControl: {
              AetherFrameKind const kind =
                  cmd.type == CommandType::kSendControl
                      ? AetherFrameKind::kControl
                      : AetherFrameKind::kApplication;
              auto& peer = peers[cmd.peer_uid];
              peer.uid_text = cmd.peer_uid;
              PendingOut item{.kind = kind, .bytes = std::move(cmd.bytes)};
              if (!peer.stream) {
                queue_pending(peer, std::move(item));
                open_peer_internal(cmd.peer_uid);
              } else if (peer.write_in_flight ||
                         (kind == AetherFrameKind::kApplication &&
                          peer.defer_large_app_until_rx &&
                          item.bytes.size() > kInitialAckAppPayloadMax)) {
                queue_pending(peer, std::move(item));
              } else {
                auto const info = peer.stream->stream_info();
                std::string reason = !info.is_writable
                                         ? "not_writable"
                                         : (info.link_state == ae::LinkState::kLinked
                                                ? "linked"
                                                : "not_linked");
                reason += " max_el=";
                reason += std::to_string(info.max_element_size);
                reason += " rec_el=";
                reason += std::to_string(info.rec_element_size);
                auto const frame_size =
                    EncodeAetherFrame(kind, item.bytes).size();
                if (info.max_element_size != 0 &&
                    frame_size > info.max_element_size) {
                  reason += " OVERSIZE";
                  JoinTrace("WRITE_OVERSIZE", "aether", reason, 0, {},
                            cmd.peer_uid, {}, {}, frame_size, 0,
                            "frame_exceeds_max_element");
                }
                char const* stage =
                    kind == AetherFrameKind::kControl ? "CTRL_TX" : "APP_TX";
                if (!write_encoded_frame(peer, kind, item.bytes, stage,
                                         reason)) {
                  queue_pending(peer, std::move(item));
                }
              }
              break;
            }
            case CommandType::kClosePeer: {
              auto it = peers.find(cmd.peer_uid);
              if (it != peers.end()) {
                it->second.data_sub.Reset();
                it->second.update_sub.Reset();
                it->second.write_subs.clear();
                it->second.stream.reset();
                it->second.stream_linked = false;
                it->second.write_in_flight = false;
                it->second.in_flight_bytes.clear();
                it->second.pending_out.clear();
                set_presence(it->second, PeerPresence::kOffline);
              }
              break;
            }
          }
        }


        // Presence uses any reassembled frame (control/app). Application
        // heartbeats share P2pSafeStream's single Write window with NodeState
        // and journal Events; a hung heartbeat Write permanently blocks them.
        auto const now_ms = CurrentSteadyTimeMs();
        for (auto& [peer_uid, peer] : peers) {
          (void)peer_uid;
          // Hang recovery: one CreatePort rebuild, not same-port rewrap.
          if (peer.write_in_flight && peer.write_started_ms != 0 &&
              now_ms - peer.write_started_ms >= kWriteHangRecoverMs &&
              peer.stream && client && !peer.pending_join_safestream_reset) {
            JoinTrace("WRITE_HANG_RECOVER", "aether", "schedule_new_port", 0, {},
                      peer.uid_text, {}, {}, peer.in_flight_bytes.size(),
                      JoinTraceHash(peer.in_flight_bytes),
                      "no_status_callback");
            auto stalled = std::move(peer.in_flight_bytes);
            auto stalled_kind = peer.in_flight_kind;
            peer.write_subs.clear();
            peer.write_in_flight = false;
            peer.write_started_ms = 0;
            if (!stalled.empty()) {
              queue_pending(peer, PendingOut{stalled_kind, std::move(stalled)});
            }
            peer.post_join_safestream_reset = false;
            peer.pending_join_safestream_reset = true;
            peer.join_reset_ready_ms = now_ms;
          }
          if (peer.reported_presence == PeerPresence::kConnecting ||
              peer.reported_presence == PeerPresence::kOnline) {
            auto const last_active =
                (std::max)(peer.last_heartbeat_rx_ms, peer.last_rx_ms);
            if (now_ms - last_active >= config.offline_after_ms) {
              set_presence(peer, PeerPresence::kOffline);
            }
          }
        }
      }

      auto const now = ae::Now();
      if (client_configured) {
        local_connectivity.Tick(now);
      }
      auto next = aether_app->Update(now);
      if (stop_) {
        break;
      }
      auto const wake_cap = now + std::chrono::milliseconds{25};
      if (next > wake_cap) {
        next = wake_cap;
      }
      aether_app->WaitUntil(next);
    }

    select_sub.Reset();
    inbound_sub.Reset();
    for (auto& [uid, peer] : peers) {
      peer.data_sub.Reset();
      peer.update_sub.Reset();
      peer.write_subs.clear();
      peer.stream.reset();
    }
    peers.clear();

    if (client) {
      aether_app->aether().Save();
    }
    aether_app->Exit(0);
  } catch (std::exception const& ex) {
    FailedCallback failed_cb;
    {
      std::lock_guard<std::mutex> lock{callback_mu_};
      failed_cb = on_failed_;
    }
    if (failed_cb) {
      failed_cb(std::string{"Aether runtime exception: "} + ex.what());
    }
  } catch (...) {
    FailedCallback failed_cb;
    {
      std::lock_guard<std::mutex> lock{callback_mu_};
      failed_cb = on_failed_;
    }
    if (failed_cb) {
      failed_cb("Aether runtime unknown exception");
    }
  }
}

}  // namespace apptraverse::example::chat_demo
