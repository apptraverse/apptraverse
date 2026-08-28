#include "aether_runtime.h"

#include <fstream>
#include <utility>

#include "aether/all.h"

#include "apptraverse/directory_domain_storage.h"

#include "chat_ids.h"
#include "chat_log.h"

namespace apptraverse {

ChatAetherRuntime::~ChatAetherRuntime() {
  RequestStop();
  Join();
}

void ChatAetherRuntime::Start(std::filesystem::path aether_state_dir,
                              UidCallback on_uid) {
  RequestStop();
  Join();
  stop_ = false;
  thread_ = std::thread(&ChatAetherRuntime::ThreadMain, this,
                        std::move(aether_state_dir), std::move(on_uid));
}

void ChatAetherRuntime::RequestStop() { stop_ = true; }

void ChatAetherRuntime::Join() {
  if (thread_.joinable()) {
    thread_.join();
  }
}

void ChatAetherRuntime::ThreadMain(std::filesystem::path aether_state_dir,
                                   UidCallback on_uid) {
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

    while (!stop_ && !aether_app->IsExited()) {
      auto next = aether_app->Update(ae::Now());
      if (stop_) {
        break;
      }
      auto const wake_cap = ae::Now() + std::chrono::milliseconds{100};
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
