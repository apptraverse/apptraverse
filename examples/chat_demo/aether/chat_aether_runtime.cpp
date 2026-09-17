#include "chat_aether_runtime.h"
#include "aether_stream_frame.h"
#include "join_delivery_trace.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
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

std::string TokenIncarnationDetail(std::uint64_t token,
                                   std::uint64_t incarnation) {
  return "token=" + std::to_string(token) +
         " incarnation=" + std::to_string(incarnation);
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

    auto set_presence = [this](PeerState& peer, PeerPresence new_presence) {
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

    auto same_pending = [](PeerState const& peer, AetherFrameKind kind,
                           std::vector<std::uint8_t> const& bytes) {
      if (peer.active_write_token != 0 && peer.active_kind == kind &&
          peer.active_payload == bytes) {
        return true;
      }
      for (auto const& item : peer.pending_out) {
        if (item.kind == kind && item.bytes == bytes) {
          return true;
        }
      }
      return false;
    };

    auto has_pending_kind = [](PeerState const& peer, AetherFrameKind kind) {
      if (peer.active_write_token != 0 && peer.active_kind == kind) {
        return true;
      }
      for (auto const& item : peer.pending_out) {
        if (item.kind == kind) {
          return true;
        }
      }
      return false;
    };

    auto queue_pending = [&same_pending](PeerState& peer, PendingOut item) {
      if (same_pending(peer, item.kind, item.bytes)) {
        return;
      }
      peer.pending_out.push_back(std::move(item));
    };

    auto destroy_peer_channel = [](PeerState& peer) {
      peer.data_sub.Reset();
      peer.update_sub.Reset();
      peer.active_write_sub.Reset();
      peer.stream.reset();
      peer.raw_p2p.reset();
      peer.stream_linked = false;
      peer.active_write_token = 0;
      peer.active_payload.clear();
      peer.terminal_notice.reset();
      peer.terminal_notice_pending = false;
    };

    auto on_stream_data =
        [this, &peers, &set_presence, &same_pending](
            std::string const& peer_uid_text, ae::DataBuffer const& data) {
          auto it = peers.find(peer_uid_text);
          if (it == peers.end()) {
            return;
          }
          auto& peer = it->second;
          std::vector<std::uint8_t> bytes(data.begin(), data.end());
          JoinTrace("RAW_RX", "aether", "before_outer_decode", 0, peer_uid_text,
                    {}, {}, {}, bytes.size(), JoinTraceHash(bytes));

          AetherFrameKind kind{};
          std::vector<std::uint8_t> payload;
          if (!DecodeAetherFrame(bytes, kind, payload)) {
            JoinTrace("RAW_RX_DECODE_FAIL", "aether", "bad_outer_frame", 0,
                      peer_uid_text, {}, {}, {}, bytes.size(),
                      JoinTraceHash(bytes), "bad outer frame");
            return;
          }

          auto const now_ms = CurrentSteadyTimeMs();
          peer.last_rx_ms = now_ms;

          if (kind == AetherFrameKind::kHeartbeatPing) {
            peer.last_heartbeat_rx_ms = now_ms;
            set_presence(peer, PeerPresence::kOnline);
            auto const nonce = DecodeHeartbeatNonce(payload);
            auto pong_bytes = EncodeHeartbeatNonce(nonce);
            // Queue/coalesce pong; never drop solely because the FIFO is
            // non-empty. Write starts only from the outer pump.
            if (!same_pending(peer, AetherFrameKind::kHeartbeatPong,
                              pong_bytes)) {
              auto& q = peer.pending_out;
              q.erase(std::remove_if(
                          q.begin(), q.end(),
                          [](PendingOut const& item) {
                            return item.kind == AetherFrameKind::kHeartbeatPong;
                          }),
                      q.end());
              peer.pending_out.push_back(
                  PendingOut{.kind = AetherFrameKind::kHeartbeatPong,
                             .bytes = std::move(pong_bytes)});
            }
            return;
          }

          if (kind == AetherFrameKind::kHeartbeatPong) {
            peer.last_heartbeat_rx_ms = now_ms;
            set_presence(peer, PeerPresence::kOnline);
            return;
          }

          if (kind == AetherFrameKind::kApplication) {
            set_presence(peer, PeerPresence::kOnline);
            JoinTrace("APP_RX", "aether", "kApplication", 0, peer_uid_text, {},
                      {}, {}, payload.size(), JoinTraceHash(payload));
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
            JoinTrace("CTRL_RX", "aether", "kControl", 0, peer_uid_text, {}, {},
                      {}, payload.size(), JoinTraceHash(payload));
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

    auto on_stream_update = [&peers, &set_presence](
                                std::string const& peer_uid_text) {
      auto it = peers.find(peer_uid_text);
      if (it == peers.end() || !it->second.stream) {
        return;
      }
      auto& peer = it->second;
      auto const link_state = peer.stream->stream_info().link_state;
      if (link_state == ae::LinkState::kLinked) {
        peer.stream_linked = true;
        // Link alone never fabricates Online.
        if (peer.reported_presence != PeerPresence::kOnline) {
          set_presence(peer, PeerPresence::kConnecting);
        }
      } else if (link_state == ae::LinkState::kLinkError ||
                 link_state == ae::LinkState::kUnlinked) {
        peer.stream_linked = false;
        if (peer.reported_presence == PeerPresence::kOnline) {
          set_presence(peer, PeerPresence::kConnecting);
        }
      }
    };

    auto bind_peer_stream = [&peers, &set_presence, &aether_app,
                             &destroy_peer_channel, &on_stream_data,
                             &on_stream_update](
                                std::string const& peer_uid_text, ae::Uid uid,
                                std::shared_ptr<ae::P2pStream> p2p_stream,
                                bool inbound) {
      (void)inbound;
      auto& peer = peers[peer_uid_text];
      peer.uid_text = peer_uid_text;
      peer.uid = uid;

      // Preserve queued frames across channel replacement.
      auto pending = std::move(peer.pending_out);
      destroy_peer_channel(peer);
      peer.pending_out = std::move(pending);
      ++peer.channel_incarnation;

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

    auto process_write_notice = [&set_presence, &config](PeerState& peer) {
      if (!peer.terminal_notice_pending) {
        return;
      }
      peer.terminal_notice_pending = false;
      if (!peer.terminal_notice.has_value()) {
        return;
      }
      auto const notice = *peer.terminal_notice;
      peer.terminal_notice.reset();

      if (notice.incarnation != peer.channel_incarnation) {
        return;
      }
      if (peer.active_write_token == 0 ||
          notice.token != peer.active_write_token) {
        return;
      }

      // Reset subscription only after the native status emission unwound.
      peer.active_write_sub.Reset();
      auto const finished_size = peer.active_payload.size();
      auto const detail =
          TokenIncarnationDetail(notice.token, notice.incarnation);
      peer.active_write_token = 0;
      peer.active_payload.clear();

      if (notice.status == ae::WriteAction::Status::kFail) {
        JoinTrace("WRITE_FAIL", "aether", detail, 0, {}, peer.uid_text, {}, {},
                  finished_size, 0, "write_action_fail");
        peer.stream_linked = false;
        auto const now_ms = CurrentSteadyTimeMs();
        auto const last_active =
            (std::max)(peer.last_heartbeat_rx_ms, peer.last_rx_ms);
        if (now_ms - last_active >= config.offline_after_ms) {
          set_presence(peer, PeerPresence::kOffline);
        } else {
          set_presence(peer, PeerPresence::kConnecting);
        }
        // Application retry remains SharedSyncRuntime's contract; do not
        // re-queue the failed payload here.
      } else if (notice.status == ae::WriteAction::Status::kSuccess) {
        JoinTrace("WRITE_OK", "aether", detail, 0, {}, peer.uid_text, {}, {},
                  finished_size, 0, {});
      } else {
        JoinTrace("WRITE_STOP", "aether", detail, 0, {}, peer.uid_text, {}, {},
                  finished_size, 0, "write_action_stop");
      }
    };

    auto try_start_write = [&peers](PeerState& peer) {
      while (peer.active_write_token == 0 && peer.stream &&
             !peer.pending_out.empty()) {
        auto& item = peer.pending_out.front();
        auto const info = peer.stream->stream_info();
        if (info.rec_element_size == 0 || !info.is_writable) {
          JoinTrace("WRITE_WAIT_LINK", "aether",
                    TokenIncarnationDetail(peer.next_write_token,
                                           peer.channel_incarnation),
                    0, {}, peer.uid_text, {}, {}, item.bytes.size(), 0,
                    info.rec_element_size == 0 ? "rec_el=0" : "not_writable");
          return;
        }

        auto frame_bytes = EncodeAetherFrame(item.kind, item.bytes);
        if (info.max_element_size != 0 &&
            frame_bytes.size() > info.max_element_size) {
          JoinTrace("WRITE_OVERSIZE", "aether",
                    TokenIncarnationDetail(peer.next_write_token,
                                           peer.channel_incarnation),
                    0, {}, peer.uid_text, {}, {}, frame_bytes.size(), 0,
                    "frame_exceeds_max_element");
          peer.pending_out.pop_front();
          continue;
        }

        std::string reason = info.link_state == ae::LinkState::kLinked
                                 ? "linked"
                                 : "not_linked";
        reason += " max_el=";
        reason += std::to_string(info.max_element_size);
        reason += " rec_el=";
        reason += std::to_string(info.rec_element_size);
        reason += " ";
        reason += TokenIncarnationDetail(peer.next_write_token,
                                         peer.channel_incarnation);

        char const* stage = "";
        if (item.kind == AetherFrameKind::kControl) {
          stage = "CTRL_TX";
        } else if (item.kind == AetherFrameKind::kApplication) {
          stage = "APP_TX";
        }

        // Move into active state before Write.
        peer.active_kind = item.kind;
        peer.active_payload = std::move(item.bytes);
        peer.pending_out.pop_front();
        peer.active_write_token = peer.next_write_token++;
        auto const token = peer.active_write_token;
        auto const incarnation = peer.channel_incarnation;
        auto const peer_uid = peer.uid_text;

        if (stage[0] != '\0') {
          JoinTrace(stage, "aether", "p2p_write", 0, {}, peer.uid_text, {}, {},
                    frame_bytes.size(), JoinTraceHash(frame_bytes), reason);
        }

        ae::DataBuffer buffer{frame_bytes.begin(), frame_bytes.end()};
        auto& action = peer.stream->Write(std::move(buffer));
        // Keep the status lambda within SmallFunction storage (no std::string).
        if (action.is_finished()) {
          // Finished before Subscribe: unstick the pump. Prefer Fail so
          // SharedSyncRuntime retries rather than inventing Success.
          peer.terminal_notice = TerminalWriteNotice{
              .token = token,
              .incarnation = incarnation,
              .status = ae::WriteAction::Status::kFail,
          };
          peer.terminal_notice_pending = true;
          JoinTrace("WRITE_ALREADY_DONE", "aether",
                    TokenIncarnationDetail(token, incarnation), 0, {},
                    peer.uid_text, {}, {}, peer.active_payload.size(), 0,
                    "finished_before_subscribe");
          return;
        }
        peer.active_write_sub = action.status_event().Subscribe(
            [&peers, token, incarnation](ae::WriteAction::Status status) {
              for (auto& [uid, live] : peers) {
                (void)uid;
                if (live.channel_incarnation != incarnation) {
                  continue;
                }
                if (live.active_write_token != token) {
                  continue;
                }
                live.terminal_notice = TerminalWriteNotice{
                    .token = token,
                    .incarnation = incarnation,
                    .status = status,
                };
                live.terminal_notice_pending = true;
                return;
              }
            });
        return;
      }
    };

    auto schedule_heartbeat_ping =
        [&queue_pending, &has_pending_kind, &next_nonce](PeerState& peer) {
          if (!peer.stream) {
            return;
          }
          // At most one unsent scheduled ping per peer.
          if (has_pending_kind(peer, AetherFrameKind::kHeartbeatPing)) {
            return;
          }
          auto nonce_payload = EncodeHeartbeatNonce(next_nonce++);
          queue_pending(peer,
                        PendingOut{.kind = AetherFrameKind::kHeartbeatPing,
                                   .bytes = std::move(nonce_payload)});
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
                  // Bind immediately so the receive consumer is ready for the
                  // first Deliver on this port.
                  if (auto it = peers.find(peer_uid_text);
                      it != peers.end() && it->second.stream) {
                    JoinTrace("NEW_PORT_REBIND", "aether",
                              "replace_existing_stream", 0, peer_uid_text);
                    auto stream = std::make_shared<ae::P2pStream>(
                        *aether_app, client.Load(), uid, std::move(handle));
                    bind_peer_stream(peer_uid_text, uid, std::move(stream),
                                     /*inbound=*/true);
                    return;
                  }
                  JoinTrace("NEW_PORT_BIND", "aether", "inbound", 0,
                            peer_uid_text);
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

      if (client_configured) {
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
              queue_pending(peer,
                            PendingOut{.kind = kind, .bytes = std::move(cmd.bytes)});
              if (!peer.stream) {
                open_peer_internal(cmd.peer_uid);
              }
              break;
            }
            case CommandType::kClosePeer: {
              auto it = peers.find(cmd.peer_uid);
              if (it != peers.end()) {
                // Genuine close: tear down outside native write/out_data stack.
                destroy_peer_channel(it->second);
                it->second.pending_out.clear();
                ++it->second.channel_incarnation;
                set_presence(it->second, PeerPresence::kOffline);
              }
              break;
            }
          }
        }

        auto const now_ms = CurrentSteadyTimeMs();
        for (auto& [peer_uid, peer] : peers) {
          (void)peer_uid;
          if (peer.stream) {
            // Periodic ping for opened peers, including Connecting/Offline.
            if (peer.last_heartbeat_tx_ms == 0 ||
                now_ms - peer.last_heartbeat_tx_ms >=
                    config.heartbeat_period_ms) {
              schedule_heartbeat_ping(peer);
              peer.last_heartbeat_tx_ms = now_ms;
            }
          }
          if (peer.reported_presence == PeerPresence::kConnecting ||
              peer.reported_presence == PeerPresence::kOnline) {
            auto const last_active =
                (std::max)(peer.last_heartbeat_rx_ms, peer.last_rx_ms);
            if (last_active != 0 &&
                now_ms - last_active >= config.offline_after_ms) {
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

      // Outer write pump: process terminal notices, then start the next Write.
      // Never start Write from native callbacks.
      if (client_configured) {
        for (auto& [peer_uid, peer] : peers) {
          (void)peer_uid;
          process_write_notice(peer);
          try_start_write(peer);
        }
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
      (void)uid;
      peer.data_sub.Reset();
      peer.update_sub.Reset();
      peer.active_write_sub.Reset();
      peer.stream.reset();
      peer.raw_p2p.reset();
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
