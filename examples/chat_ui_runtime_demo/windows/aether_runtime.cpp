#include "aether_runtime.h"

#include <chrono>
#include <fstream>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>

#include "aether/all.h"
#include "aether/ae_actions/query_peer_receive_schedule.h"

#include "apptraverse/directory_domain_storage.h"

#include "chat_ids.h"
#include "chat_log.h"
#include "chat_presence.h"
#include "remote_presence_poller.h"

namespace apptraverse {
namespace {

std::int64_t StartupEpochMs() {
  static auto const epoch = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now().time_since_epoch())
                              .count();
  return epoch;
}

std::int64_t ElapsedSinceStartupMs() {
  auto const now = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                       .count();
  return now - StartupEpochMs();
}

class LocalConnectivityMonitor {
 public:
  void Configure(ae::Client::ptr client,
                 ChatAetherRuntime::PresenceCallback on_presence) {
    client_ = std::move(client);
    on_presence_ = std::move(on_presence);
    last_reported_online_.reset();
  }

  void Tick(ae::TimePoint now) {
    if (!client_ || !client_->connectivity_policy().is_valid()) {
      return;
    }
    auto const& policy = client_->connectivity_policy().Load();
    bool const online = policy->IsLocallyOnline(now);
    if (last_reported_online_.has_value() &&
        *last_reported_online_ == online) {
      return;
    }
    last_reported_online_ = online;
    chat::ChatLog(std::string{"LOCAL_CONNECTIVITY t_ms="} +
                  std::to_string(ElapsedSinceStartupMs()) + " online=" +
                  (online ? "1" : "0"));
    chat::ChatLog(online ? "LOCAL_PRESENCE state=online"
                         : "LOCAL_PRESENCE state=offline");
    if (on_presence_) {
      on_presence_(online);
    }
  }

 private:
  ae::Client::ptr client_;
  ChatAetherRuntime::PresenceCallback on_presence_;
  std::optional<bool> last_reported_online_;
};

class AetherRemotePresenceMonitor {
 public:
  void Configure(ae::Client::ptr client, std::string local_uid,
                 ChatAetherRuntime::PeerPresenceCallback on_presence) {
    client_ = std::move(client);
    local_uid_ = std::move(local_uid);
    on_presence_ = std::move(on_presence);
  }

  void Monitor(std::string remote_uid) {
    poller_.Monitor(std::move(remote_uid), local_uid_);
  }

  void Stop() {
    result_sub_.Reset();
    poller_.Stop();
    active_uid_.clear();
  }

  void Tick(std::chrono::steady_clock::time_point now) {
    if (!client_) {
      return;
    }
    poller_.Tick(now, [this](std::string const& uid) { return StartQuery(uid); });
  }

 private:
  bool StartQuery(std::string const& remote_uid) {
    if (!active_uid_.empty()) {
      return false;
    }
    if (remote_uid.empty() || remote_uid == local_uid_) {
      return false;
    }
    auto peer = ae::Uid::FromString(remote_uid);
    active_uid_ = remote_uid;
    auto& action = client_->QueryPeerReceiveSchedule(peer);
    result_sub_ = action.result_event().Subscribe(
        [this, remote_uid](ae::Result<ae::PeerReceiveSchedule, int> const& res) {
          bool online = false;
          if (res) {
            online = OnlineFromPeerScheduleState(
                static_cast<std::uint32_t>(res.value().state));
          } else {
            online = OnlineFromQuerySuccess(false, kPeerScheduleStateUnknown);
          }
          chat::ChatLog(std::string{"REMOTE_PRESENCE_QUERY peer="} + remote_uid +
                        " online=" + (online ? "1" : "0"));
          if (on_presence_) {
            on_presence_(remote_uid, online);
          }
          active_uid_.clear();
          poller_.OnQueryFinished(remote_uid,
                                  std::chrono::steady_clock::now());
        });
    return true;
  }

  ae::Client::ptr client_;
  std::string local_uid_;
  ChatAetherRuntime::PeerPresenceCallback on_presence_;
  RemotePresencePoller poller_;
  ae::Subscription result_sub_;
  std::string active_uid_;
};

ae::AetherAppContext MakeAetherAppContext(
    std::shared_ptr<std::filesystem::path> const& state_dir_holder) {
  ae::AetherAppContext context{[state_dir_holder] {
    return std::unique_ptr<ae::IDomainStorage>{
        std::make_unique<DirectoryDomainStorage>(*state_dir_holder)};
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

struct PeerStreamState {
  std::shared_ptr<ae::P2pStream> stream;
  ae::Subscription data_sub;
  ae::Subscription update_sub;
  bool ready{false};
  bool inbound{false};
  std::vector<std::vector<std::uint8_t>> pending_out;
};

class PeerStreamHub {
 public:
  PeerStreamHub(ae::Aether::ptr aether, ae::Client::ptr client,
                ChatAetherRuntime::PeerReadyCallback on_ready,
                ChatAetherRuntime::PeerClosedCallback on_closed,
                ChatAetherRuntime::PeerFrameCallback on_frame,
                std::function<void(std::string const&)> on_peer_seen)
      : aether_{std::move(aether)},
        client_{std::move(client)},
        on_ready_{std::move(on_ready)},
        on_closed_{std::move(on_closed)},
        on_frame_{std::move(on_frame)},
        on_peer_seen_{std::move(on_peer_seen)} {
    inbound_sub_ = client_->message_stream_manager().new_port_event().Subscribe(
        [this](ae::P2pPortHandle handle) {
          auto const uid_text = ae::Format("{}", handle.destination());
          chat::ChatLog("SHARED_STREAM_INBOUND peer=" + uid_text);
          AcceptInbound(std::move(handle));
        });
  }

  void OpenPeer(std::string const& remote_uid) {
    if (remote_uid.empty()) {
      return;
    }
    if (on_peer_seen_) {
      on_peer_seen_(remote_uid);
    }
    auto it = peers_.find(remote_uid);
    if (it != peers_.end() && it->second.stream) {
      if (it->second.ready && on_ready_) {
        on_ready_(remote_uid);
      }
      return;
    }
    auto uid = ae::Uid::FromString(remote_uid);
    chat::ChatLog("SHARED_STREAM_OPENING peer=" + remote_uid);
    auto handle = client_->message_stream_manager().CreatePort(uid);
    auto stream = std::make_shared<ae::P2pStream>(*aether_, client_.Load(), uid,
                                                  std::move(handle));
    BindStream(remote_uid, std::move(stream), /*inbound=*/false);
  }

  void SendFrame(std::string const& remote_uid,
                 std::vector<std::uint8_t> bytes) {
    auto it = peers_.find(remote_uid);
    if (it == peers_.end() || !it->second.stream) {
      return;
    }
    if (!it->second.ready) {
      it->second.pending_out.push_back(std::move(bytes));
      return;
    }
    WriteNow(*it->second.stream, std::move(bytes));
  }

  void ClosePeer(std::string const& remote_uid) {
    auto it = peers_.find(remote_uid);
    if (it == peers_.end()) {
      return;
    }
    it->second.stream.reset();
    peers_.erase(it);
    chat::ChatLog("SHARED_STREAM_CLOSED peer=" + remote_uid);
    if (on_closed_) {
      on_closed_(remote_uid);
    }
  }

 private:
  static void WriteNow(ae::P2pStream& stream, std::vector<std::uint8_t> bytes) {
    ae::DataBuffer buffer{bytes.begin(), bytes.end()};
    stream.Write(std::move(buffer)).status_event().Subscribe([](auto status) {
      if (status == ae::WriteAction::Status::kFail) {
        chat::ChatLog("SHARED_EVENT_SEND_FAIL");
      }
    });
  }

  void FlushPending(PeerStreamState& state) {
    for (auto& bytes : state.pending_out) {
      WriteNow(*state.stream, std::move(bytes));
    }
    state.pending_out.clear();
  }

  void AcceptInbound(ae::P2pPortHandle handle) {
    auto const uid_text = ae::Format("{}", handle.destination());
    if (on_peer_seen_) {
      on_peer_seen_(uid_text);
    }
    auto it = peers_.find(uid_text);
    if (it != peers_.end() && it->second.stream && !it->second.inbound) {
      chat::ChatLog("SHARED_STREAM_DUPLICATE_IGNORED peer=" + uid_text);
      return;
    }
    auto stream = std::make_shared<ae::P2pStream>(
        *aether_, client_.Load(), handle.destination(), std::move(handle));
    BindStream(uid_text, std::move(stream), /*inbound=*/true);
  }

  void BindStream(std::string const& remote_uid,
                  std::shared_ptr<ae::P2pStream> stream, bool inbound) {
    PeerStreamState state;
    state.inbound = inbound;
    state.stream = std::move(stream);
    state.data_sub = state.stream->out_data_event().Subscribe(
        [this, remote_uid](ae::DataBuffer const& data) {
          if (!on_frame_) {
            return;
          }
          std::vector<std::uint8_t> bytes{data.begin(), data.end()};
          on_frame_(remote_uid, std::move(bytes));
        });
    state.update_sub = state.stream->stream_update_event().Subscribe(
        [this, remote_uid]() {
          auto it = peers_.find(remote_uid);
          if (it == peers_.end() || it->second.ready) {
            return;
          }
          it->second.ready = true;
          FlushPending(it->second);
          if (on_ready_) {
            on_ready_(remote_uid);
          }
        });
    peers_[remote_uid] = std::move(state);
  }

  ae::Aether::ptr aether_;
  ae::Client::ptr client_;
  ChatAetherRuntime::PeerReadyCallback on_ready_;
  ChatAetherRuntime::PeerClosedCallback on_closed_;
  ChatAetherRuntime::PeerFrameCallback on_frame_;
  std::function<void(std::string const&)> on_peer_seen_;
  ae::Subscription inbound_sub_;
  std::unordered_map<std::string, PeerStreamState> peers_;
};

}  // namespace

ChatAetherRuntime::~ChatAetherRuntime() {
  RequestStop();
  Join();
}

void ChatAetherRuntime::Start(std::filesystem::path aether_state_dir,
                              UidCallback on_uid,
                              PresenceCallback on_presence) {
  RequestStop();
  Join();
  stop_ = false;
  thread_ = std::thread(&ChatAetherRuntime::ThreadMain, this,
                        std::move(aether_state_dir), std::move(on_uid),
                        std::move(on_presence));
}

void ChatAetherRuntime::SetPeerCallbacks(PeerReadyCallback on_ready,
                                         PeerClosedCallback on_closed,
                                         PeerFrameCallback on_frame) {
  std::lock_guard<std::mutex> lock{callback_mu_};
  on_peer_ready_ = std::move(on_ready);
  on_peer_closed_ = std::move(on_closed);
  on_peer_frame_ = std::move(on_frame);
}

void ChatAetherRuntime::SetPeerPresenceCallback(
    PeerPresenceCallback on_peer_presence) {
  std::lock_guard<std::mutex> lock{callback_mu_};
  on_peer_presence_ = std::move(on_peer_presence);
}

void ChatAetherRuntime::Enqueue(Command command) {
  {
    std::lock_guard<std::mutex> lock{command_mu_};
    commands_.push(std::move(command));
  }
}

void ChatAetherRuntime::OpenPeer(std::string remote_uid) {
  Enqueue(Command{.type = CommandType::kOpenPeer,
                  .remote_uid = std::move(remote_uid)});
}

void ChatAetherRuntime::SendPeerFrame(std::string remote_uid,
                                      std::vector<std::uint8_t> bytes) {
  Enqueue(Command{.type = CommandType::kSendFrame,
                  .remote_uid = std::move(remote_uid),
                  .bytes = std::move(bytes)});
}

void ChatAetherRuntime::ClosePeer(std::string remote_uid) {
  Enqueue(Command{.type = CommandType::kClosePeer,
                  .remote_uid = std::move(remote_uid)});
}

void ChatAetherRuntime::MonitorPeerPresence(std::string remote_uid) {
  Enqueue(Command{.type = CommandType::kMonitorPresence,
                  .remote_uid = std::move(remote_uid)});
}

void ChatAetherRuntime::RequestStop() { stop_ = true; }

void ChatAetherRuntime::Join() {
  if (thread_.joinable()) {
    thread_.join();
  }
}

void ChatAetherRuntime::ThreadMain(std::filesystem::path aether_state_dir,
                                   UidCallback on_uid,
                                   PresenceCallback on_presence) {
  try {
    std::filesystem::create_directories(aether_state_dir);
    auto state_dir_holder =
        std::make_shared<std::filesystem::path>(std::move(aether_state_dir));
    auto aether_app = ae::AetherApp::Construct(MakeAetherAppContext(state_dir_holder));

    auto const parent =
        ae::Uid::FromString(std::string{chat::kAetherParentUid});
    ae::Client::ptr client;
    auto& select =
        aether_app->aether()->SelectClient(parent, chat::kAetherClientName);
    select.result_event().Subscribe(
        [&](ae::Result<ae::Client::ptr, int> const& res) {
          if (!res) {
            chat::ChatLog("aether SelectClient failed code=" +
                          std::to_string(res.error()));
            aether_app->Exit(1);
            return;
          }
          client = res.value();
        });
    aether_app->WaitActions(select);
    if (!client) {
      chat::ChatLog("aether client missing after SelectClient");
      return;
    }

    std::string const uid_text = ae::Format("{}", client->uid());
    chat::ChatLog("AETHER_CLIENT_READY t_ms=" +
                  std::to_string(ElapsedSinceStartupMs()) + " uid=" + uid_text);

    auto constexpr kPingInterval = std::chrono::seconds{1};
    auto constexpr kReceiveWindow = std::chrono::seconds{3};
    auto const schedule_result = client->SetReceiveSchedule(ae::ReceiveSchedule{
        .ping_interval =
            std::chrono::duration_cast<ae::Duration>(kPingInterval),
        .receive_window =
            std::chrono::duration_cast<ae::Duration>(kReceiveWindow),
    });
    if (!schedule_result) {
      chat::ChatLog("aether SetReceiveSchedule failed code=" +
                    std::to_string(schedule_result.error()));
    } else {
      chat::ChatLog("AETHER_RX_SCHEDULE_SET t_ms=" +
                    std::to_string(ElapsedSinceStartupMs()) +
                    " ping_ms=1000 window_ms=3000");
    }

    static_cast<void>(client->cloud_connection());
    chat::ChatLog("AETHER_CLOUD_CONNECTION t_ms=" +
                  std::to_string(ElapsedSinceStartupMs()));

    aether_app->aether().Save();  // runtime-save-ok

    chat::ChatLog("aether client uid=" + uid_text);
    {
      auto uid_path = *state_dir_holder / "last_uid.txt";
      std::ofstream out{uid_path, std::ios::out | std::ios::trunc};
      out << uid_text;
    }
    if (on_uid) {
      on_uid(uid_text);
    }

    ChatAetherRuntime::PeerReadyCallback on_ready;
    ChatAetherRuntime::PeerClosedCallback on_closed;
    ChatAetherRuntime::PeerFrameCallback on_frame;
    ChatAetherRuntime::PeerPresenceCallback on_peer_presence;
    {
      std::lock_guard<std::mutex> lock{callback_mu_};
      on_ready = on_peer_ready_;
      on_closed = on_peer_closed_;
      on_frame = on_peer_frame_;
      on_peer_presence = on_peer_presence_;
    }

    AetherRemotePresenceMonitor remote_presence;
    remote_presence.Configure(client, uid_text, std::move(on_peer_presence));

    PeerStreamHub hub{aether_app->aether(), client, std::move(on_ready),
                      std::move(on_closed), std::move(on_frame),
                      [&](std::string const& remote_uid) {
                        remote_presence.Monitor(remote_uid);
                      }};

    LocalConnectivityMonitor presence;
    if (on_presence) {
      presence.Configure(client, std::move(on_presence));
    }

    while (!stop_ && !aether_app->IsExited()) {
      {
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
              hub.OpenPeer(cmd.remote_uid);
              break;
            case CommandType::kSendFrame:
              hub.SendFrame(cmd.remote_uid, std::move(cmd.bytes));
              break;
            case CommandType::kClosePeer:
              hub.ClosePeer(cmd.remote_uid);
              break;
            case CommandType::kMonitorPresence:
              remote_presence.Monitor(cmd.remote_uid);
              break;
          }
        }
      }

      auto const now = ae::Now();
      auto next = aether_app->Update(now);
      presence.Tick(ae::Now());
      remote_presence.Tick(std::chrono::steady_clock::now());
      if (stop_) {
        break;
      }
      auto const wake_cap = now + std::chrono::milliseconds{50};
      if (next > wake_cap) {
        next = wake_cap;
      }
      aether_app->WaitUntil(next);
    }
    remote_presence.Stop();
    aether_app->aether().Save();  // runtime-save-ok
    aether_app->Exit(0);
  } catch (std::exception const& ex) {
    chat::ChatLog(std::string{"aether runtime exception: "} + ex.what());
  } catch (...) {
    chat::ChatLog("aether runtime unknown exception");
  }
}

}  // namespace apptraverse
