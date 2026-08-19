#include "linux_runtime.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <system_error>
#include <utility>

#include "aether/clock.h"
#include "aether/obj/obj.h"

#include "apptraverse/directory_domain_storage.h"

#include "aether_p2p_transport.h"
#include "aether_runtime.h"
#include "chat_component.h"
#include "graph_builder.h"
#include "linux_window.h"
#include "model/application_ids.h"
#include "model/chat.h"
#include "model/registration.h"

namespace apptraverse::linux_host {
namespace {

APPTRAVERSE_REGISTER(LinuxWindow);
APPTRAVERSE_REGISTER(LinuxWindowPresenter);
APPTRAVERSE_REGISTER(LinuxChatPresenter);

constexpr std::chrono::milliseconds kMaxIdleWait{20};

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

void LogError(std::string const& line) {
  std::cerr << line << '\n';
}

}  // namespace

LinuxRuntime::LinuxRuntime(std::string state_dir, UiSink ui)
    : state_dir_{std::move(state_dir)}, ui_{std::move(ui)} {}

LinuxRuntime::~LinuxRuntime() = default;

void LinuxRuntime::Run() {
  if (!Setup()) {
    if (ui_.post_error) {
      ui_.post_error("Native runtime failed to start");
    }
    return;
  }

  while (!stop_requested_.load(std::memory_order::acquire)) {
    auto const now = ae::Now();
    auto const next_time = aether_app_->Update(now);
    if (chat_component_ != nullptr) {
      chat_component_->Tick(now);
    }
    DrainPendingSends();
    DrainPendingPeers();
    if (stop_requested_.load(std::memory_order::acquire)) {
      break;
    }
    aether_app_->WaitUntil(std::min(next_time, ae::Now() + kMaxIdleWait));
  }

  Teardown();
}

bool LinuxRuntime::QueueSend(std::string text) {
  auto trimmed = Trim(std::move(text));
  if (trimmed.empty()) {
    return false;
  }
  {
    auto lock = std::scoped_lock{pending_lock_};
    pending_sends_.push_back(std::move(trimmed));
  }
  auto* scheduler = scheduler_.load(std::memory_order::acquire);
  if (scheduler != nullptr) {
    scheduler->Task([this]() { DrainPendingSends(); });
  }
  return true;
}

bool LinuxRuntime::QueueAddPeer(std::string uid) {
  auto trimmed = Trim(std::move(uid));
  if (trimmed.empty()) {
    return false;
  }
  {
    auto lock = std::scoped_lock{pending_lock_};
    pending_peers_.push_back(std::move(trimmed));
  }
  auto* scheduler = scheduler_.load(std::memory_order::acquire);
  if (scheduler != nullptr) {
    scheduler->Task([this]() { DrainPendingPeers(); });
  }
  return true;
}

void LinuxRuntime::Stop() {
  stop_requested_.store(true, std::memory_order::release);
  WakeUp();
}

bool LinuxRuntime::Setup() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureSingleClientChatRegistration();

  auto ec = std::error_code{};
  std::filesystem::create_directories(std::filesystem::path{state_dir_}, ec);
  if (ec) {
    LogError("Failed to create the state directory " + state_dir_);
    return false;
  }

  auto storage_root = state_dir_;
  auto runtime = examples::ConstructAetherAppWithEthernet([storage_root]() {
    return std::make_unique<DirectoryDomainStorage>(
        std::filesystem::path{storage_root});
  });
  aether_app_ = std::move(runtime.app);
  domain_storage_ = runtime.storage;
  if (aether_app_.get() == nullptr || domain_storage_ == nullptr) {
    LogError("Failed to construct AetherApp");
    return false;
  }

  scheduler_.store(aether_app_->aether()->task_scheduler.get(),
                   std::memory_order::release);

  if (!LoadOrBuildGraph()) {
    return false;
  }
  if (!SelectAetherClient()) {
    return false;
  }

  StartP2pTransport();

  if (!LoadPresenters()) {
    return false;
  }
  if (!StartChatSync()) {
    return false;
  }

  if (chat_presenter_ != nullptr && chat_component_ != nullptr) {
    chat_presenter_->PublishPresentation(
        chat_component_->CapturePresentation());
  }
  DrainPendingSends();
  DrainPendingPeers();
  return true;
}

bool LinuxRuntime::LoadOrBuildGraph() {
  auto& domain = aether_app_->domain();

  app_ = App::ptr::Declare(ae::CreateWith{domain}.with_id(
      ToObjId(ApplicationObjId::Application)));
  app_.Load();
  if (app_.is_loaded() && app_->window.is_valid()) {
    return true;
  }

  auto graph =
      examples::BuildSingleClientChatGraph<LinuxWindow, LinuxWindowPresenter,
                                           LinuxChatPresenter>(domain,
                                                               "Linux");
  app_ = graph.app;
  if (!app_.is_valid()) {
    LogError("Failed to build the single client chat graph");
    return false;
  }
  SaveState();
  return true;
}

bool LinuxRuntime::SelectAetherClient() {
  int select_error = 0;
  aether_client_ = examples::SelectPersistentAetherClient(
      *aether_app_, examples::kLinuxAetherClientName, &select_error);
  if (!aether_client_) {
    LogError("Failed to select Aether client error=" +
             std::to_string(select_error));
    return false;
  }
  if (ui_.post_local_uid) {
    ui_.post_local_uid(examples::FormatAetherUid(aether_client_->uid()));
  }
  return true;
}

void LinuxRuntime::StartP2pTransport() {
  p2p_transport_ = std::make_unique<examples::AetherP2pTransport>();
  p2p_transport_->Start(*aether_app_, aether_client_);
}

bool LinuxRuntime::StartChatSync() {
  if (chat_presenter_ == nullptr || domain_storage_ == nullptr ||
      p2p_transport_ == nullptr) {
    LogError("Chat sync prerequisites missing");
    return false;
  }

  chat_presenter_->chat.Load();
  if (!chat_presenter_->chat.is_loaded()) {
    LogError("Failed to load Chat for sync");
    return false;
  }
  auto chat = chat_presenter_->chat;

  auto sync_send = [this](ae::Uid const& peer, ae::ObjId,
                          SerializedSyncPacket const& bytes) {
    p2p_transport_->Send(peer, bytes);
  };
  auto presence_send = [this](ae::Uid const& peer,
                              std::vector<std::uint8_t> const& bytes) {
    p2p_transport_->Send(peer, bytes);
  };

  auto local_client = chat_presenter_->local_client;
  local_client.Load();
  if (!local_client.is_loaded()) {
    LogError("Failed to load local Client for chat component");
    return false;
  }

  chat_component_ = std::make_unique<chat::ChatComponent>(
      SyncReplica{aether_app_->domain(), *domain_storage_, chat.id()},
      local_client, chat, sync_send, presence_send,
      [this](ae::Uid const& remote_uid) {
        if (p2p_transport_ != nullptr) {
          p2p_transport_->Connect(remote_uid);
        }
      },
      chat::ChatSyncTiming{}, false);

  chat_component_->SubscribePresentationChanged([this]() {
    if (chat_presenter_ != nullptr && chat_component_ != nullptr) {
      chat_presenter_->PublishPresentation(
          chat_component_->CapturePresentation());
    }
    SaveState();
  });

  p2p_transport_->SetReceiveHandler(
      [this](ae::Uid const& peer, std::vector<std::uint8_t> const& payload) {
        if (examples::TryHandleP2pProbePayload(
                *p2p_transport_, peer, payload,
                std::function<void(std::string const&)>{})) {
          return;
        }
        if (chat_component_ != nullptr) {
          chat_component_->Receive(peer, payload);
        }
      });
  chat_component_->Start();
  return true;
}

bool LinuxRuntime::LoadPresenters() {
  auto window = app_->window;
  window.Load();
  if (!window.is_loaded()) {
    LogError("Failed to load Window");
    return false;
  }

  auto presenter = window->presenter;
  presenter.Load();
  if (!presenter.is_loaded() ||
      presenter->GetClassId() != LinuxWindowPresenter::kClassId) {
    LogError("Expected LinuxWindowPresenter in the loaded graph");
    return false;
  }

  window_presenter_ = &static_cast<LinuxWindowPresenter&>(*presenter);
  chat_presenter_ = window_presenter_->LoadLinuxChatPresenter();
  if (chat_presenter_ == nullptr) {
    LogError("Expected LinuxChatPresenter in the loaded graph");
    return false;
  }

  chat_presenter_->chat.Load();
  if (!chat_presenter_->chat.is_loaded()) {
    LogError("Failed to load Chat");
    return false;
  }

  chat_presenter_->SetTranscriptPublisher(
      [this](std::string const& text) { PublishTranscript(text); });
  return true;
}

void LinuxRuntime::Teardown() {
  if (chat_component_ != nullptr) {
    chat_component_->Stop();
  }
  if (chat_presenter_ != nullptr) {
    SaveState();
    chat_presenter_ = nullptr;
  }
  window_presenter_ = nullptr;
  chat_component_.reset();
  p2p_transport_.reset();
  aether_client_.Reset();
  domain_storage_ = nullptr;
  scheduler_.store(nullptr, std::memory_order::release);
  app_.Reset();
  aether_app_.reset();
}

void LinuxRuntime::DrainPendingSends() {
  auto texts = std::vector<std::string>{};
  {
    auto lock = std::scoped_lock{pending_lock_};
    texts.swap(pending_sends_);
  }
  if (texts.empty()) {
    return;
  }
  if (chat_component_ == nullptr) {
    auto lock = std::scoped_lock{pending_lock_};
    pending_sends_.insert(pending_sends_.begin(), texts.begin(), texts.end());
    return;
  }

  for (auto const& text : texts) {
    if (chat_component_->SubmitText(text).has_value()) {
      SaveState();
    }
  }
}

void LinuxRuntime::DrainPendingPeers() {
  auto peers = std::vector<std::string>{};
  {
    auto lock = std::scoped_lock{pending_lock_};
    peers.swap(pending_peers_);
  }
  if (peers.empty()) {
    return;
  }
  if (chat_component_ == nullptr || p2p_transport_ == nullptr ||
      !aether_client_) {
    auto lock = std::scoped_lock{pending_lock_};
    pending_peers_.insert(pending_peers_.begin(), peers.begin(), peers.end());
    return;
  }

  auto const local_uid = aether_client_->uid();
  for (auto const& text : peers) {
    auto const uid = ae::Uid::FromString(std::string_view{text});
    if (uid.empty() || uid == local_uid) {
      continue;
    }
    chat_component_->AddPeer(uid);
    SaveState();
  }
}

void LinuxRuntime::PublishTranscript(std::string const& transcript) {
  if (ui_.post_transcript) {
    ui_.post_transcript(transcript);
  }
}

void LinuxRuntime::SaveState() {
  if (!app_.is_valid()) {
    return;
  }
  app_.Save();
}

void LinuxRuntime::WakeUp() {
  auto* scheduler = scheduler_.load(std::memory_order::acquire);
  if (scheduler != nullptr) {
    scheduler->Task([]() {});
  }
}

}  // namespace apptraverse::linux_host
