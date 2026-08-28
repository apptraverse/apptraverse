#include "aether_runtime.h"

#include <chrono>
#include <fstream>
#include <optional>
#include <utility>

#include "aether/all.h"

#include "apptraverse/directory_domain_storage.h"

#include "chat_ids.h"
#include "chat_log.h"

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

    LocalConnectivityMonitor presence;
    if (on_presence) {
      presence.Configure(client, std::move(on_presence));
    }

    while (!stop_ && !aether_app->IsExited()) {
      auto const now = ae::Now();
      auto next = aether_app->Update(now);
      presence.Tick(ae::Now());
      if (stop_) {
        break;
      }
      auto const wake_cap = now + std::chrono::milliseconds{100};
      if (next > wake_cap) {
        next = wake_cap;
      }
      aether_app->WaitUntil(next);
    }
    aether_app->aether().Save();  // runtime-save-ok
    aether_app->Exit(0);
  } catch (std::exception const& ex) {
    chat::ChatLog(std::string{"aether runtime exception: "} + ex.what());
  } catch (...) {
    chat::ChatLog("aether runtime unknown exception");
  }
}

}  // namespace apptraverse
