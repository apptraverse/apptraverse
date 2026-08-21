#include "linux_runtime.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "aether/all.h"
#include "aether/domain_storage/ram_domain_storage.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/domain_snapshot_io.h"

#include "aether_p2p_transport.h"
#include "aether_runtime.h"
#include "chat_component.h"
#include "chat_transcript.h"
#include "graph_builder.h"
#include "linux_chat_presenter.h"
#include "linux_window.h"
#include "linux_window_presenter.h"
#include "model/app.h"
#include "model/application_ids.h"
#include "model/chat.h"
#include "model/chat_peer_set.h"
#include "model/client.h"
#include "model/registration.h"

namespace apptraverse::linux_host {
namespace {

APPTRAVERSE_REGISTER(LinuxWindow);
APPTRAVERSE_REGISTER(LinuxWindowPresenter);
APPTRAVERSE_REGISTER(LinuxChatPresenter);

constexpr auto kBusinessIdleCap = std::chrono::milliseconds{100};
constexpr auto kNetworkIdleCap = std::chrono::seconds{1};

using chat::ChatComponent;
using examples::AetherP2pTransport;
using examples::FormatAetherUid;
using apptraverse::SerializedSyncPacket;

enum class ShutdownPhase : std::uint8_t {
  kRunning = 0,
  kStoppingBusiness = 1,
  kStoppingNetwork = 2,
  kFinalizingModel = 3,
  kStopped = 4,
};

std::string Trim(std::string text) {
  auto const is_space = [](char symbol) {
    return symbol == ' ' || symbol == '\t' || symbol == '\r' || symbol == '\n';
  };
  while (!text.empty() && is_space(text.front())) {
    text.erase(text.begin());
  }
  while (!text.empty() && is_space(text.back())) {
    text.pop_back();
  }
  return text;
}

void LogError(std::string const& line) { std::cerr << line << '\n'; }

std::filesystem::path AetherRoot(std::filesystem::path const& state_dir) {
  return state_dir / "aether";
}

std::filesystem::path ModelRoot(std::filesystem::path const& state_dir) {
  return state_dir / "model";
}

struct SubmitTextCommand {
  std::string text;
};

struct AddPeerCommand {
  ae::Uid uid;
};

struct InboundNetworkPacket {
  ae::Uid peer;
  std::vector<std::uint8_t> bytes;
};

struct NetworkReadyEvent {};
struct BeginShutdownCommand {};
struct FinalizeShutdownCommand {};

using BusinessItem =
    std::variant<SubmitTextCommand, AddPeerCommand, InboundNetworkPacket,
                 NetworkReadyEvent, BeginShutdownCommand,
                 FinalizeShutdownCommand>;

struct ConnectPeerCommand {
  ae::Uid uid;
};

struct SendSyncCommand {
  ae::Uid peer;
  ae::ObjId packet_id;
  SerializedSyncPacket bytes;
};

struct SendRawCommand {
  ae::Uid peer;
  std::vector<std::uint8_t> bytes;
};

struct StopNetworkCommand {};

using NetworkItem =
    std::variant<ConnectPeerCommand, SendSyncCommand, SendRawCommand,
                 StopNetworkCommand>;

template <typename T>
class WakeQueue {
 public:
  void Push(T item) {
    {
      std::scoped_lock lock{mu_};
      items_.push_back(std::move(item));
    }
    cv_.notify_one();
  }

  template <typename Pred>
  bool WaitPop(T& out, Pred should_stop, std::chrono::milliseconds max_wait) {
    std::unique_lock lock{mu_};
    cv_.wait_for(lock, max_wait, [&] {
      return should_stop() || !items_.empty();
    });
    if (items_.empty()) {
      return false;
    }
    out = std::move(items_.front());
    items_.pop_front();
    return true;
  }

  bool TryPop(T& out) {
    std::scoped_lock lock{mu_};
    if (items_.empty()) {
      return false;
    }
    out = std::move(items_.front());
    items_.pop_front();
    return true;
  }

  void Notify() { cv_.notify_one(); }

 private:
  std::mutex mu_;
  std::condition_variable cv_;
  std::deque<T> items_;
};

bool DistillFreshState(std::filesystem::path const& state_dir) {
  auto const model_root = ModelRoot(state_dir);
  auto const aether_root = AetherRoot(state_dir);
  std::error_code ec;
  std::filesystem::create_directories(model_root, ec);
  if (ec) {
    LogError("Failed to create model directory");
    return false;
  }
  std::filesystem::create_directories(aether_root, ec);
  if (ec) {
    LogError("Failed to create aether directory");
    return false;
  }

  ae::RamDomainStorage model_ram;
  ae::Domain model_domain{ae::Now(), model_ram};
  auto graph =
      examples::BuildSingleClientChatGraph<LinuxWindow, LinuxWindowPresenter,
                                           LinuxChatPresenter>(model_domain,
                                                               "Linux");
  graph.app.Save();
  graph.chat.Save();
  graph.peer_set.Save();
  graph.local_client.Save();
  if (graph.window.is_valid()) {
    graph.window.Save();
  }
  SaveDirectorySnapshot(model_ram, model_root);

  auto runtime = examples::ConstructAetherAppWithEthernet([aether_root]() {
    return std::make_unique<DirectoryDomainStorage>(aether_root);
  });
  if (runtime.app) {
    (void)runtime.app->Update(ae::Now());
  }
  return true;
}

void FinalizeModelToRam(App::ptr app_ptr, Chat::ptr chat_ptr) {
  chat_ptr.Load();
  if (chat_ptr.is_loaded()) {
    chat_ptr.Save();
    auto peer_set = chat_ptr->peer_set;
    peer_set.Load();
    if (peer_set.is_loaded()) {
      peer_set.Save();
      for (auto& peer : peer_set->peers) {
        if (!peer.session_state.is_valid()) {
          continue;
        }
        peer.session_state.Load();
        if (peer.session_state.is_loaded()) {
          peer.session_state.Save();
        }
      }
    }
  }
  if (!app_ptr.is_valid()) {
    return;
  }
  app_ptr.Load();
  if (!app_ptr.is_loaded()) {
    return;
  }
  if (app_ptr->window.is_valid()) {
    app_ptr->window.Load();
    if (app_ptr->window.is_loaded()) {
      app_ptr->window.Save();
    }
  }
  if (app_ptr->local_client.is_valid()) {
    app_ptr->local_client.Load();
    if (app_ptr->local_client.is_loaded()) {
      app_ptr->local_client.Save();
    }
  }
  app_ptr.Save();
}

}  // namespace

struct LinuxRuntime::Impl {
  std::string state_dir;
  UiSink ui;

  std::atomic<ShutdownPhase> phase{ShutdownPhase::kRunning};
  std::atomic<bool> ui_accepting{true};
  std::atomic<bool> run_started{false};
  std::atomic<bool> component_stop_done{false};
  std::atomic<bool> network_join_done{false};
  std::atomic<bool> finalize_done{false};
  std::atomic<bool> snapshot_saved{false};
  std::atomic<bool> reject_inbound{false};
  std::atomic<ae::TaskScheduler*> scheduler{nullptr};

  std::mutex phase_mu;
  std::condition_variable phase_cv;

  WakeQueue<BusinessItem> business_q;
  WakeQueue<NetworkItem> network_q;
};

LinuxRuntime::LinuxRuntime(std::string state_dir, UiSink ui)
    : impl_{std::make_unique<Impl>()} {
  impl_->state_dir = std::move(state_dir);
  impl_->ui = std::move(ui);
}

LinuxRuntime::~LinuxRuntime() = default;

bool LinuxRuntime::QueueSend(std::string text) {
  if (impl_ == nullptr || !impl_->ui_accepting.load(std::memory_order::acquire) ||
      !impl_->run_started.load(std::memory_order::acquire) ||
      impl_->phase.load(std::memory_order::acquire) !=
          ShutdownPhase::kRunning) {
    return false;
  }
  auto trimmed = Trim(std::move(text));
  if (trimmed.empty()) {
    return false;
  }
  impl_->business_q.Push(SubmitTextCommand{std::move(trimmed)});
  return true;
}

bool LinuxRuntime::QueueAddPeer(std::string uid) {
  if (impl_ == nullptr || !impl_->ui_accepting.load(std::memory_order::acquire) ||
      !impl_->run_started.load(std::memory_order::acquire) ||
      impl_->phase.load(std::memory_order::acquire) !=
          ShutdownPhase::kRunning) {
    return false;
  }
  auto trimmed = Trim(std::move(uid));
  if (trimmed.empty()) {
    return false;
  }
  auto const peer = ae::Uid::FromString(std::string_view{trimmed});
  if (peer.empty()) {
    return false;
  }
  impl_->business_q.Push(AddPeerCommand{peer});
  return true;
}

void LinuxRuntime::Stop() {
  if (impl_ == nullptr) {
    return;
  }
  auto& impl = *impl_;
  impl.ui_accepting.store(false, std::memory_order::release);

  auto expected = ShutdownPhase::kRunning;
  if (!impl.phase.compare_exchange_strong(expected,
                                          ShutdownPhase::kStoppingBusiness,
                                          std::memory_order::acq_rel,
                                          std::memory_order::acquire)) {
    return;
  }

  impl.business_q.Push(BeginShutdownCommand{});
  impl.business_q.Notify();
  impl.phase_cv.notify_all();
}

void LinuxRuntime::Run() {
  auto& impl = *impl_;
  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureSingleClientChatRegistration();
  ResetDomainSnapshotIoStats();

  std::filesystem::path const state_dir{impl.state_dir};
  auto const model_root = ModelRoot(state_dir);
  auto const aether_root = AetherRoot(state_dir);

  std::error_code ec;
  std::filesystem::create_directories(state_dir, ec);
  if (ec) {
    LogError("Failed to create the state directory " + impl.state_dir);
    if (impl.ui.post_error) {
      impl.ui.post_error("Native runtime failed to start");
    }
    return;
  }

  if (!std::filesystem::exists(aether_root) ||
      !std::filesystem::exists(model_root)) {
    if (!DistillFreshState(state_dir)) {
      if (impl.ui.post_error) {
        impl.ui.post_error("Native runtime failed to distill state");
      }
      return;
    }
  }

  // Bootstrap SelectClient on this thread, then hand ownership to network.
  auto aether_runtime = examples::ConstructAetherAppWithEthernet(
      [aether_root]() {
        return std::make_unique<DirectoryDomainStorage>(aether_root);
      });
  if (!aether_runtime.app) {
    LogError("Failed to construct AetherApp");
    if (impl.ui.post_error) {
      impl.ui.post_error("Native runtime failed to start");
    }
    return;
  }

  int select_error = 0;
  auto aether_client = examples::SelectPersistentAetherClient(
      *aether_runtime.app, examples::kLinuxAetherClientName, &select_error);
  if (!aether_client) {
    LogError("Failed to select Aether client, error=" +
             std::to_string(select_error));
    if (impl.ui.post_error) {
      impl.ui.post_error("Native runtime failed to start");
    }
    return;
  }
  auto const local_uid = FormatAetherUid(aether_client->uid());
  std::cout << "AETHER_CLIENT_READY platform=linux uid=" << local_uid << '\n';
  std::fflush(stdout);
  if (impl.ui.post_local_uid) {
    impl.ui.post_local_uid(local_uid);
  }

  struct NetworkOwned {
    std::unique_ptr<ae::AetherApp> app;
    ae::Client::ptr client;
  };
  auto network_owned = std::make_shared<NetworkOwned>();
  network_owned->app = std::move(aether_runtime.app);
  network_owned->client = aether_client;
  // Drop bootstrap-only non-owning pointer; network thread owns lifetime.
  aether_client = {};

  auto model_storage = std::make_unique<ae::RamDomainStorage>();
  LoadDirectorySnapshot(model_root, *model_storage);
  auto model_domain =
      std::make_unique<ae::Domain>(ae::Now(), *model_storage);

  auto app = App::ptr::Declare(ae::CreateWith{*model_domain}.with_id(
      ToObjId(ApplicationObjId::Application)));
  app.Load();
  if (!app.is_loaded() || !app->window.is_valid()) {
    auto graph =
        examples::BuildSingleClientChatGraph<LinuxWindow, LinuxWindowPresenter,
                                             LinuxChatPresenter>(*model_domain,
                                                                 "Linux");
    app = graph.app;
    app.Save();
  }

  auto local_client = app->local_client;
  local_client.Load();
  if (!local_client.is_loaded()) {
    LogError("Failed to load App.local_client");
    if (impl.ui.post_error) {
      impl.ui.post_error("Native runtime failed to start");
    }
    return;
  }

  auto window = app->window;
  window.Load();
  auto presenter = window->presenter;
  presenter.Load();
  auto& linux_presenter = static_cast<LinuxWindowPresenter&>(*presenter);
  linux_presenter.chat_presenter.Load();
  auto& chat_presenter_obj =
      static_cast<LinuxChatPresenter&>(*linux_presenter.chat_presenter);
  chat_presenter_obj.chat.Load();
  auto chat = chat_presenter_obj.chat;

  std::atomic<bool> network_ready{false};
  auto const local_aether_uid = network_owned->client->uid();

  auto wake_network = [&]() {
    auto* sch = impl.scheduler.load(std::memory_order::acquire);
    if (sch != nullptr) {
      sch->Task([]() {});
    }
    impl.network_q.Notify();
  };

  auto wait_flag = [&](std::atomic<bool> const& flag) {
    std::unique_lock lock{impl.phase_mu};
    impl.phase_cv.wait(lock, [&] { return flag.load(std::memory_order::acquire); });
  };

  auto publish_presentation = [&](ChatComponent& component) {
    if (impl.phase.load(std::memory_order::acquire) !=
        ShutdownPhase::kRunning) {
      return;
    }
    auto const transcript =
        examples::FormatChatPresentationUtf8(component.CapturePresentation());
    if (impl.ui.post_transcript) {
      impl.ui.post_transcript(transcript);
    }
  };

  impl.run_started.store(true, std::memory_order::release);

  std::thread network_thread([&, network_owned]() {
    auto owned = network_owned;
    auto transport = std::make_unique<AetherP2pTransport>();
    transport->Start(*owned->app, owned->client);
    impl.scheduler.store(owned->app->aether()->task_scheduler.get(),
                         std::memory_order::release);

    transport->SetReceiveHandler(
        [&](ae::Uid const& peer, std::vector<std::uint8_t> const& payload) {
          if (impl.reject_inbound.load(std::memory_order::acquire)) {
            return;
          }
          if (examples::TryHandleP2pProbePayload(*transport, peer, payload, {},
                                                 {})) {
            return;
          }
          impl.business_q.Push(InboundNetworkPacket{peer, payload});
        });

    network_ready.store(true, std::memory_order::release);
    impl.business_q.Push(NetworkReadyEvent{});

    for (;;) {
      NetworkItem item;
      bool got = false;
      while (impl.network_q.TryPop(item)) {
        got = true;
        bool stop_now = false;
        std::visit(
            [&](auto&& cmd) {
              using T = std::decay_t<decltype(cmd)>;
              if constexpr (std::is_same_v<T, ConnectPeerCommand>) {
                if (transport) {
                  transport->Connect(cmd.uid);
                }
              } else if constexpr (std::is_same_v<T, SendSyncCommand>) {
                if (transport) {
                  transport->Send(cmd.peer, cmd.bytes);
                }
              } else if constexpr (std::is_same_v<T, SendRawCommand>) {
                if (transport) {
                  transport->Send(cmd.peer, cmd.bytes);
                }
              } else if constexpr (std::is_same_v<T, StopNetworkCommand>) {
                stop_now = true;
              }
            },
            item);
        if (stop_now) {
          goto network_teardown;
        }
      }

      if (!got) {
        // Sleep until scheduler wake or idle cap; Stop always wake_network().
        auto const now = ae::Now();
        auto const next = owned->app->Update(now);
        if (owned->app->IsExited()) {
          break;
        }
        // Re-check stop commands that arrived during Update.
        if (impl.network_q.TryPop(item)) {
          impl.network_q.Push(std::move(item));
          continue;
        }
        owned->app->WaitUntil(std::min(next, ae::Now() + kNetworkIdleCap));
      } else {
        (void)owned->app->Update(ae::Now());
      }
    }

  network_teardown:
    impl.reject_inbound.store(true, std::memory_order::release);

    if (transport) {
      transport->SetReceiveHandler({});
    }

    transport.reset();

    impl.scheduler.store(nullptr, std::memory_order::release);

    if (owned->app && !owned->app->IsExited()) {
      owned->app->Exit(0);
    }

    // Drain Aether until Exit completes on this thread only.
    while (owned->app && !owned->app->IsExited()) {
      auto const now = ae::Now();
      auto const next = owned->app->Update(now);
      if (owned->app->IsExited()) {
        break;
      }
      owned->app->WaitUntil(std::min(next, ae::Now() + std::chrono::milliseconds{20}));
    }

    owned->client = {};
    owned->app.reset();

    impl.network_join_done.store(true, std::memory_order::release);
    impl.phase_cv.notify_all();
  });

  std::thread business_thread([&]() {
    auto const shutting_down = [&] {
      return impl.phase.load(std::memory_order::acquire) !=
             ShutdownPhase::kRunning;
    };

    while (!network_ready.load(std::memory_order::acquire) &&
           !shutting_down()) {
      BusinessItem boot;
      if (impl.business_q.WaitPop(boot, shutting_down,
                                  std::chrono::milliseconds{50})) {
        if (std::holds_alternative<NetworkReadyEvent>(boot)) {
          break;
        }
        if (std::holds_alternative<BeginShutdownCommand>(boot) ||
            std::holds_alternative<FinalizeShutdownCommand>(boot)) {
          impl.business_q.Push(std::move(boot));
          break;
        }
        impl.business_q.Push(std::move(boot));
      }
    }

    // Early shutdown before component start.
    if (!network_ready.load(std::memory_order::acquire) && shutting_down()) {
      impl.component_stop_done.store(true, std::memory_order::release);
      impl.phase_cv.notify_all();
      while (!impl.finalize_done.load(std::memory_order::acquire)) {
        BusinessItem fin;
        if (impl.business_q.WaitPop(
                fin,
                [&] {
                  return impl.finalize_done.load(std::memory_order::acquire);
                },
                std::chrono::milliseconds{50})) {
          if (std::holds_alternative<FinalizeShutdownCommand>(fin)) {
            if (!impl.snapshot_saved.exchange(true,
                                              std::memory_order::acq_rel)) {
              SaveDirectorySnapshot(*model_storage, model_root);
            }
            impl.finalize_done.store(true, std::memory_order::release);
            impl.phase_cv.notify_all();
            break;
          }
        }
      }
      return;
    }

    // Lexical scope: ChatComponent (and SyncReplica) must die before Run()
    // clears Domain ptrs / Domain / storage.
    {
    ChatComponent component(
        SyncReplica{*model_domain, *model_storage, chat.id()}, local_client,
        chat,
        [&](ae::Uid const& peer, ae::ObjId packet_id,
            SerializedSyncPacket const& bytes) {
          if (impl.reject_inbound.load(std::memory_order::acquire) ||
              impl.phase.load(std::memory_order::acquire) >=
                  ShutdownPhase::kStoppingNetwork) {
            return;
          }
          impl.network_q.Push(SendSyncCommand{peer, packet_id, bytes});
          wake_network();
        },
        [&](ae::Uid const& peer, std::vector<std::uint8_t> const& bytes) {
          if (impl.reject_inbound.load(std::memory_order::acquire) ||
              impl.phase.load(std::memory_order::acquire) >=
                  ShutdownPhase::kStoppingNetwork) {
            return;
          }
          impl.network_q.Push(SendRawCommand{peer, bytes});
          wake_network();
        },
        [&](ae::Uid const& remote_uid) {
          if (impl.reject_inbound.load(std::memory_order::acquire) ||
              impl.phase.load(std::memory_order::acquire) >=
                  ShutdownPhase::kStoppingNetwork) {
            return;
          }
          impl.network_q.Push(ConnectPeerCommand{remote_uid});
          wake_network();
        },
        chat::ChatSyncTiming{},
        [&](std::string const& line) {
          std::cout << line << '\n';
          std::fflush(stdout);
        });

    component.Start();
    publish_presentation(component);

    bool component_stopped_once = false;
    bool exit_loop = false;
    while (!exit_loop) {
      BusinessItem item;
      bool const got = impl.business_q.WaitPop(
          item,
          [&] { return impl.finalize_done.load(std::memory_order::acquire); },
          kBusinessIdleCap);
      auto const now = ae::Now();
      if (got) {
        std::visit(
            [&](auto&& cmd) {
              using T = std::decay_t<decltype(cmd)>;
              if constexpr (std::is_same_v<T, SubmitTextCommand>) {
                if (impl.phase.load(std::memory_order::acquire) !=
                    ShutdownPhase::kRunning) {
                  return;
                }
                auto const event_id = component.SubmitText(cmd.text);
                if (event_id.has_value()) {
                  std::cout << "CHAT_MESSAGE_COMMITTED platform=linux event="
                            << *event_id << " text_key=" << cmd.text << '\n';
                  std::fflush(stdout);
                }
                publish_presentation(component);
              } else if constexpr (std::is_same_v<T, AddPeerCommand>) {
                if (impl.phase.load(std::memory_order::acquire) !=
                    ShutdownPhase::kRunning) {
                  return;
                }
                if (cmd.uid == local_aether_uid) {
                  return;
                }
                component.AddPeer(cmd.uid);
                publish_presentation(component);
              } else if constexpr (std::is_same_v<T, InboundNetworkPacket>) {
                // Drain inbound even during finalize prelude.
                component.Receive(cmd.peer, cmd.bytes);
                if (impl.phase.load(std::memory_order::acquire) ==
                    ShutdownPhase::kRunning) {
                  publish_presentation(component);
                }
              } else if constexpr (std::is_same_v<T, NetworkReadyEvent>) {
              } else if constexpr (std::is_same_v<T, BeginShutdownCommand>) {
                if (!component_stopped_once) {
                  component.Stop();
                  component_stopped_once = true;
                }
                impl.component_stop_done.store(true, std::memory_order::release);
                impl.phase_cv.notify_all();
              } else if constexpr (std::is_same_v<T, FinalizeShutdownCommand>) {
                // Drain any inbound still queued before RAM/snapshot.
                BusinessItem extra;
                while (impl.business_q.TryPop(extra)) {
                  if (auto* inbound =
                          std::get_if<InboundNetworkPacket>(&extra)) {
                    component.Receive(inbound->peer, inbound->bytes);
                  }
                }
                FinalizeModelToRam(app, chat);
                if (!impl.snapshot_saved.exchange(true,
                                                  std::memory_order::acq_rel)) {
                  SaveDirectorySnapshot(*model_storage, model_root);
                }
                impl.finalize_done.store(true, std::memory_order::release);
                impl.phase_cv.notify_all();
                exit_loop = true;
              }
            },
            item);
      }

      if (!component_stopped_once &&
          impl.phase.load(std::memory_order::acquire) ==
              ShutdownPhase::kRunning) {
        component.Tick(now);
      }
    }

    }  // ChatComponent destroyed here (SyncReplica + model ptr copies).
  });

  // Wait until Stop() begins ordered shutdown (GTK close).
  {
    std::unique_lock lock{impl.phase_mu};
    impl.phase_cv.wait(lock, [&] {
      return impl.phase.load(std::memory_order::acquire) !=
             ShutdownPhase::kRunning;
    });
  }

  // STOPPING_BUSINESS: wait for ChatComponent::Stop (BeginShutdown from Stop()).
  wait_flag(impl.component_stop_done);

  // STOPPING_NETWORK: only network thread exits Aether / destroys transport.
  {
    auto expected = ShutdownPhase::kStoppingBusiness;
    impl.phase.compare_exchange_strong(expected, ShutdownPhase::kStoppingNetwork,
                                       std::memory_order::acq_rel,
                                       std::memory_order::acquire);
  }
  impl.network_q.Push(StopNetworkCommand{});
  wake_network();
  wait_flag(impl.network_join_done);
  if (network_thread.joinable()) {
    network_thread.join();
  }

  // FINALIZING_MODEL: business drains inbound, RAM save, one snapshot.
  {
    auto expected = ShutdownPhase::kStoppingNetwork;
    impl.phase.compare_exchange_strong(expected, ShutdownPhase::kFinalizingModel,
                                       std::memory_order::acq_rel,
                                       std::memory_order::acquire);
  }
  impl.business_q.Push(FinalizeShutdownCommand{});
  impl.business_q.Notify();
  wait_flag(impl.finalize_done);
  if (business_thread.joinable()) {
    business_thread.join();
  }

  // Run()-stack Domain handles outlive ChatComponent until here. Destroy them
  // before Domain/storage (explicit resets were previously done too early).
  chat.Reset();
  presenter.Reset();
  window.Reset();
  local_client.Reset();
  app.Reset();

  model_domain.reset();
  model_storage.reset();
  network_owned.reset();

  impl.phase.store(ShutdownPhase::kStopped, std::memory_order::release);
  impl.run_started.store(false, std::memory_order::release);
}

}  // namespace apptraverse::linux_host
