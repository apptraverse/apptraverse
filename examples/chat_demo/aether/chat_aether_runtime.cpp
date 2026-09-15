#include "chat_aether_runtime.h"
#include "aether_stream_frame.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "aether/adapters/ethernet.h"
#include "aether/global_ids.h"
#include "aether/tele_statistics.h"
#include "ae-numeric/percentile.h"

#include "apptraverse/directory_domain_storage.h"

namespace apptraverse::example::chat_demo {
namespace {

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

struct PeerState {
  std::string uid_text;
  ae::Uid uid;

  std::shared_ptr<ae::P2pStream> stream;

  ae::Subscription data_sub;
  ae::Subscription update_sub;

  std::vector<ae::Subscription> write_subs;

  std::deque<std::vector<std::uint8_t>> pending_out;

  bool stream_linked{false};

  std::uint64_t last_rx_ms{0};
  std::uint64_t last_heartbeat_tx_ms{0};
  std::uint64_t last_heartbeat_rx_ms{0};

  PeerPresence reported_presence{PeerPresence::kUnknown};
};

}  // namespace

ChatAetherRuntime::ChatAetherRuntime() = default;

ChatAetherRuntime::~ChatAetherRuntime() {
  RequestStop();
  Join();
}

void ChatAetherRuntime::Start(Config config, LocalUidCallback on_uid,
                              ReadyCallback on_ready, FailedCallback on_failed,
                              FrameCallback on_frame,
                              PresenceCallback on_presence) {
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
  thread_ = std::thread(&ChatAetherRuntime::ThreadMain, this, std::move(config),
                        std::move(on_uid), std::move(on_ready),
                        std::move(on_failed), std::move(on_frame),
                        std::move(on_presence));
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
                                  PresenceCallback on_presence) {
  {
    std::lock_guard<std::mutex> lock{callback_mu_};
    on_uid_ = std::move(on_uid);
    on_ready_ = std::move(on_ready);
    on_failed_ = std::move(on_failed);
    on_frame_ = std::move(on_frame);
    on_presence_ = std::move(on_presence);
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

    auto on_write_status = [this, &peers, &set_presence, &config](
                               std::string const& peer_uid_text,
                               ae::WriteAction::Status status) {
      if (status == ae::WriteAction::Status::kSuccess) {
        return;
      }
      if (status == ae::WriteAction::Status::kFail) {
        auto it = peers.find(peer_uid_text);
        if (it != peers.end()) {
          it->second.stream_linked = false;
          auto const now_ms = CurrentSteadyTimeMs();
          auto const last_active = (std::max)(it->second.last_heartbeat_rx_ms,
                                              it->second.last_rx_ms);
          if (now_ms - last_active >= config.offline_after_ms) {
            set_presence(it->second, PeerPresence::kOffline);
          } else {
            set_presence(it->second, PeerPresence::kConnecting);
          }
        }
      }
    };

    auto send_heartbeat = [this, &on_write_status](PeerState& peer,
                                                  AetherFrameKind kind,
                                                  std::uint64_t nonce) {
      if (!peer.stream) {
        return;
      }
      auto nonce_payload = EncodeHeartbeatNonce(nonce);
      auto bytes = EncodeAetherFrame(kind, nonce_payload);
      ae::DataBuffer buffer{bytes.begin(), bytes.end()};
      auto& action = peer.stream->Write(std::move(buffer));
      peer.write_subs.push_back(action.status_event().Subscribe(
          [this, &on_write_status, peer_uid = peer.uid_text](
              ae::WriteAction::Status status) {
            on_write_status(peer_uid, status);
          }));
      if (peer.write_subs.size() > 128) {
        peer.write_subs.erase(peer.write_subs.begin(),
                              peer.write_subs.begin() + 64);
      }
    };

    auto flush_pending_out = [this, &on_write_status](PeerState& peer) {
      if (!peer.stream) {
        return;
      }
      while (!peer.pending_out.empty()) {
        auto raw_bytes = std::move(peer.pending_out.front());
        peer.pending_out.pop_front();
        auto frame_bytes = EncodeAetherFrame(AetherFrameKind::kApplication, raw_bytes);
        ae::DataBuffer buffer{frame_bytes.begin(), frame_bytes.end()};
        auto& action = peer.stream->Write(std::move(buffer));
        peer.write_subs.push_back(action.status_event().Subscribe(
            [this, &on_write_status, peer_uid = peer.uid_text](
                ae::WriteAction::Status status) {
              on_write_status(peer_uid, status);
            }));
      }
      if (peer.write_subs.size() > 128) {
        peer.write_subs.erase(peer.write_subs.begin(),
                              peer.write_subs.begin() + 64);
      }
    };

    auto on_stream_data = [this, &peers, &set_presence, &send_heartbeat](
                              std::string const& peer_uid_text,
                              ae::DataBuffer const& data) {
      auto it = peers.find(peer_uid_text);
      if (it == peers.end()) {
        return;
      }
      auto& peer = it->second;
      std::vector<std::uint8_t> bytes(data.begin(), data.end());

      AetherFrameKind kind{};
      std::vector<std::uint8_t> payload;
      if (!DecodeAetherFrame(bytes, kind, payload)) {
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

        FrameCallback cb;
        {
          std::lock_guard<std::mutex> lock{callback_mu_};
          cb = on_frame_;
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
                             &on_stream_data, &on_stream_update](
                                std::string const& peer_uid_text, ae::Uid uid,
                                std::shared_ptr<ae::P2pStream> stream,
                                bool inbound) {
      (void)inbound;
      auto& peer = peers[peer_uid_text];
      peer.uid_text = peer_uid_text;
      peer.uid = uid;

      peer.data_sub.Reset();
      peer.update_sub.Reset();
      peer.write_subs.clear();

      peer.stream = std::move(stream);
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
          *aether_app->aether(), client.Load(), uid, std::move(handle));
      bind_peer_stream(peer_uid_text, uid, std::move(stream), /*inbound=*/false);
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
                  auto it = peers.find(peer_uid_text);
                  if (it != peers.end() && it->second.stream) {
                    bool const is_linked =
                        it->second.stream_linked ||
                        (it->second.stream->stream_info().link_state ==
                         ae::LinkState::kLinked);
                    if (is_linked) {
                      return;
                    }
                  }
                  auto stream = std::make_shared<ae::P2pStream>(
                      *aether_app->aether(), client.Load(), uid,
                      std::move(handle));
                  bind_peer_stream(peer_uid_text, uid, std::move(stream),
                                   /*inbound=*/true);
                });

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
            case CommandType::kSend: {
              auto it = peers.find(cmd.peer_uid);
              if (it == peers.end() || !it->second.stream) {
                auto& peer = peers[cmd.peer_uid];
                peer.uid_text = cmd.peer_uid;
                if (peer.pending_out.empty() || peer.pending_out.back() != cmd.bytes) {
                  peer.pending_out.push_back(std::move(cmd.bytes));
                }
                open_peer_internal(cmd.peer_uid);
              } else {
                auto frame_bytes = EncodeAetherFrame(
                    AetherFrameKind::kApplication, cmd.bytes);
                ae::DataBuffer buffer{frame_bytes.begin(), frame_bytes.end()};
                auto& action = it->second.stream->Write(std::move(buffer));
                it->second.write_subs.push_back(
                    action.status_event().Subscribe(
                        [this, &on_write_status, peer_uid = cmd.peer_uid](
                            ae::WriteAction::Status status) {
                          on_write_status(peer_uid, status);
                        }));
                if (it->second.write_subs.size() > 128) {
                  it->second.write_subs.erase(
                      it->second.write_subs.begin(),
                      it->second.write_subs.begin() + 64);
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
                it->second.pending_out.clear();
                set_presence(it->second, PeerPresence::kOffline);
              }
              break;
            }
          }
        }

        // Heartbeat scheduling & timeout check
        auto const now_ms = CurrentSteadyTimeMs();
        for (auto& [peer_uid, peer] : peers) {
          if (peer.stream) {
            if (now_ms - peer.last_heartbeat_tx_ms >=
                config.heartbeat_period_ms) {
              peer.last_heartbeat_tx_ms = now_ms;
              send_heartbeat(peer, AetherFrameKind::kHeartbeatPing, next_nonce++);
            }
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
