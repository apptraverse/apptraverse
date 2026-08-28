#include "aether_runtime.h"

#include <fstream>
#include <optional>
#include <utility>

#include "aether/all.h"
#include "aether/ae_actions/query_peer_receive_schedule.h"

#include "apptraverse/directory_domain_storage.h"

#include "chat_ids.h"
#include "chat_log.h"
#include "chat_presence.h"

namespace apptraverse {
namespace {

auto constexpr kPresenceQueryPeriod = std::chrono::seconds{3};

char const* PeerScheduleStateName(ae::PeerScheduleState state) {
  switch (state) {
    case ae::PeerScheduleState::kExpected:
      return "Expected";
    case ae::PeerScheduleState::kMissedDeadline:
      return "MissedDeadline";
    case ae::PeerScheduleState::kUnknown:
      return "Unknown";
  }
  return "Unknown";
}

std::int64_t MsSince(ae::TimePoint earlier, ae::TimePoint later) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(later - earlier)
      .count();
}

void LogPresenceQuery(ae::Uid const& uid,
                      ae::Result<ae::PeerReceiveSchedule, int> const& res) {
  auto const now = ae::Now();
  if (!res) {
    chat::ChatLog("PRESENCE_QUERY uid=" + ae::Format("{}", uid) +
                  " success=0 state=error last_online_delta_ms=0 " +
                  "next_ping_delta_ms=0 error=" +
                  std::to_string(res.error()));
    return;
  }
  auto const& schedule = res.value();
  auto const last_online_delta_ms = MsSince(schedule.last_online, now);
  auto const next_ping_delta_ms =
      schedule.next_ping_deadline.has_value()
          ? MsSince(now, *schedule.next_ping_deadline)
          : 0;
  chat::ChatLog(
      "PRESENCE_QUERY uid=" + ae::Format("{}", uid) + " success=1 state=" +
      PeerScheduleStateName(schedule.state) +
      " last_online_delta_ms=" + std::to_string(last_online_delta_ms) +
      " next_ping_delta_ms=" + std::to_string(next_ping_delta_ms));
}

class LocalPresencePoller {
 public:
  void Configure(ae::Client::ptr client, ae::Uid local_uid,
                 ChatAetherRuntime::PresenceCallback on_presence) {
    client_ = std::move(client);
    local_uid_ = local_uid;
    on_presence_ = std::move(on_presence);
    query_inflight_ = false;
    last_reported_online_.reset();
    next_query_at_ = ae::Now();
  }

  void Tick(ae::TimePoint now) {
    if (!client_ || query_inflight_ || now < next_query_at_) {
      return;
    }
    BeginQuery();
  }

 private:
  void BeginQuery() {
    if (!client_ || query_inflight_) {
      return;
    }
    query_inflight_ = true;
    query_sub_.Reset();
    auto& action = client_->QueryPeerReceiveSchedule(local_uid_);
    query_sub_ = action.result_event().Subscribe(
        [this](ae::Result<ae::PeerReceiveSchedule, int> const& res) {
          OnQueryResult(res);
        });
  }

  void OnQueryResult(ae::Result<ae::PeerReceiveSchedule, int> const& res) {
    query_inflight_ = false;
    next_query_at_ = ae::Now() + kPresenceQueryPeriod;

    LogPresenceQuery(local_uid_, res);

    bool const online =
        res ? OnlineFromPeerScheduleState(
                  static_cast<std::uint32_t>(res.value().state))
            : false;

    if (last_reported_online_.has_value() &&
        *last_reported_online_ == online) {
      return;
    }
    last_reported_online_ = online;
    chat::ChatLog(online ? "LOCAL_PRESENCE state=online"
                         : "LOCAL_PRESENCE state=offline");
    if (on_presence_) {
      on_presence_(online);
    }
  }

  ae::Client::ptr client_;
  ae::Uid local_uid_{};
  ChatAetherRuntime::PresenceCallback on_presence_;
  ae::Subscription query_sub_;
  bool query_inflight_{false};
  std::optional<bool> last_reported_online_;
  ae::TimePoint next_query_at_{};
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
    auto aether_app = ae::AetherApp::Construct(
        ae::AetherAppContext{[state_dir_holder] {
          return std::unique_ptr<ae::IDomainStorage>{
              std::make_unique<DirectoryDomainStorage>(*state_dir_holder)};
        }}
#if AE_DISTILLATION
            .AddAdapterFactory([](ae::AetherAppContext const& context) {
              return ae::EthernetAdapter::ptr::Create(
                  ae::CreateWith{context.domain()}.with_id(
                      ae::GlobalId::kEthernetAdapter),
                  context.aether(), context.poller(), context.dns_resolver());
            })
#endif
    );

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
    chat::ChatLog("AETHER_CLIENT_READY uid=" + uid_text);

    auto constexpr kPingInterval = std::chrono::seconds{3};
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
      chat::ChatLog("AETHER_RX_SCHEDULE_SET ping_ms=3000 window_ms=3000");
    }

    static_cast<void>(client->cloud_connection());

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

    LocalPresencePoller presence;
    if (on_presence) {
      presence.Configure(client, client->uid(), std::move(on_presence));
    }

    while (!stop_ && !aether_app->IsExited()) {
      auto const now = ae::Now();
      auto next = aether_app->Update(now);
      presence.Tick(now);
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
