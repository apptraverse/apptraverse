#include "apple_chat_runtime.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <system_error>

#include "aether/clock.h"
#include "aether/obj/obj.h"

#include "model/application_ids.h"
#include "model/app.h"
#include "model/chat.h"
#include "model/registration.h"
#include "apptraverse/directory_domain_storage.h"

#include "../../common/aether_p2p_transport.h"
#include "../../common/aether_runtime.h"
#include "../../common/chat_component.h"
#include "../../common/graph_builder.h"
#include "apple_log.h"
#include "apple_window.h"
#include "apple_window_presenter.h"

#include "model/chat_presenter.h"

namespace apptraverse::apple {
namespace {

APPTRAVERSE_REGISTER(AppleWindow);
APPTRAVERSE_REGISTER(AppleWindowPresenter);
APPTRAVERSE_REGISTER(AppleChatPresenter);

constexpr std::chrono::milliseconds kMaxIdleWait{200};

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

}  // namespace

AppleChatRuntime::AppleChatRuntime(std::string state_dir,
                                   std::string aether_client_name,
                                   std::string local_client_name,
                                   UiCallbacks callbacks)
    : state_dir_{std::move(state_dir)},
      aether_client_name_{std::move(aether_client_name)},
      local_client_name_{std::move(local_client_name)},
      callbacks_{std::move(callbacks)} {
  LogMarker("APPLE_RUNTIME_CREATED state_dir=" + state_dir_);
}

AppleChatRuntime::~AppleChatRuntime() = default;

void AppleChatRuntime::Run() {
  if (!Setup()) {
    LogError("Apple chat runtime failed to start");
    return;
  }

  while (!stop_requested_.load(std::memory_order::acquire)) {
    auto const now = ae::Now();
    auto const next_time = aether_app_->Update(now);
    if (chat_component_ != nullptr) {
      chat_component_->Tick(now);
    }
    if (stop_requested_.load(std::memory_order::acquire)) {
      break;
    }
    aether_app_->WaitUntil(std::min(next_time, ae::Now() + kMaxIdleWait));
  }

  Teardown();
}

bool AppleChatRuntime::QueueSend(std::string text) {
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

bool AppleChatRuntime::QueueAddPeer(std::string uid) {
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

void AppleChatRuntime::Stop() {
  stop_requested_.store(true, std::memory_order::release);
  WakeUp();
}

bool AppleChatRuntime::Setup() {
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
  LogMarker("AETHER_RUNTIME_READY");

  scheduler_.store(aether_app_->aether()->task_scheduler.get(),
                   std::memory_order::release);

  if (!LoadOrBuildGraph()) {
    return false;
  }

  auto* const aether_domain = &aether_app_->domain();
  auto* const app_domain = app_.domain();
  LogMarker("SINGLE_DOMAIN_READY aether_domain=" + PointerToHex(aether_domain) +
            " app_domain=" + PointerToHex(app_domain) + " match=" +
            (aether_domain == app_domain ? "1" : "0"));
  if (aether_domain != app_domain) {
    LogError("Aether and the application graph are in different Domains");
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

bool AppleChatRuntime::LoadOrBuildGraph() {
  auto& domain = aether_app_->domain();

  app_ = App::ptr::Declare(ae::CreateWith{domain}.with_id(
      ToObjId(ApplicationObjId::Application)));
  app_.Load();
  if (app_.is_loaded() && app_->window.is_valid()) {
    LogMarker("APPLE_GRAPH_LOADED");
    return true;
  }

  auto graph = examples::BuildSingleClientChatGraph<
      AppleWindow, AppleWindowPresenter, AppleChatPresenter>(
      domain, local_client_name_);
  app_ = graph.app;
  if (!app_.is_valid()) {
    LogError("Failed to build the single client chat graph");
    return false;
  }
  LogMarker("APPLE_GRAPH_CREATED");
  SaveState();
  return true;
}

bool AppleChatRuntime::SelectAetherClient() {
  int select_error = 0;
  LogMarker("AETHER_SELECT_CLIENT_START name=" + aether_client_name_);
  aether_client_ = examples::SelectPersistentAetherClient(
      *aether_app_, aether_client_name_, &select_error);
  if (!aether_client_) {
    LogError("Failed to select Aether client error=" +
             std::to_string(select_error) +
             " exited=" + (aether_app_->IsExited() ? "1" : "0"));
    return false;
  }
  auto const uid = examples::FormatAetherUid(aether_client_->uid());
  LogMarker("AETHER_CLIENT_READY platform=apple uid=" + uid);
  if (callbacks_.on_aether_uid) {
    callbacks_.on_aether_uid(uid);
  }
  return true;
}

void AppleChatRuntime::StartP2pTransport() {
  p2p_transport_ = std::make_unique<examples::AetherP2pTransport>();
  p2p_transport_->SetLogHandler([](std::string line) { LogMarker(line); });
  p2p_transport_->Start(*aether_app_, aether_client_);
  LogMarker("AETHER_P2P_TRANSPORT_READY");
}

bool AppleChatRuntime::StartChatSync() {
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

  auto sync_send = [this](ae::Uid const& peer, ae::ObjId packet_id,
                          SerializedSyncPacket const& bytes) {
    p2p_transport_->Send(peer, bytes);
    LogMarker("SYNC_TRANSPORT_WRITE peer=" + examples::FormatAetherUid(peer) +
              " packet=" + std::to_string(packet_id.id()));
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
      chat::ChatSyncTiming{},
      [](std::string const& line) { LogMarker(line); });
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
                [](std::string const& line) { LogMarker(line); })) {
          return;
        }
        if (chat_component_ != nullptr) {
          chat_component_->Receive(peer, payload);
        }
      });
  chat_component_->Start();

  LogMarker("CHAT_SYNC_CONTROLLER_READY");
  return true;
}

bool AppleChatRuntime::LoadPresenters() {
  auto window = app_->window;
  window.Load();
  if (!window.is_loaded()) {
    LogError("Failed to load Window");
    return false;
  }

  auto presenter = window->presenter;
  presenter.Load();
  if (!presenter.is_loaded() ||
      presenter->GetClassId() != AppleWindowPresenter::kClassId) {
    LogError("Expected AppleWindowPresenter in the loaded graph");
    return false;
  }

  window_presenter_ = &static_cast<AppleWindowPresenter&>(*presenter);
  chat_presenter_ = window_presenter_->LoadAppleChatPresenter();
  if (chat_presenter_ == nullptr) {
    LogError("Expected AppleChatPresenter in the loaded graph");
    return false;
  }

  chat_presenter_->chat.Load();
  if (!chat_presenter_->chat.is_loaded()) {
    LogError("Failed to load Chat");
    return false;
  }

  chat_presenter_->SetTranscriptPublisher(
      [this](std::string const& text) { PublishTranscript(text); });
  LogMarker("APPLE_PRESENTERS_LOADED");
  return true;
}

void AppleChatRuntime::Teardown() {
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

void AppleChatRuntime::DrainPendingSends() {
  auto texts = std::vector<std::string>{};
  {
    auto lock = std::scoped_lock{pending_lock_};
    texts.swap(pending_sends_);
  }
  if (texts.empty()) {
    return;
  }
  if (chat_component_ == nullptr) {
    LogError("Dropping queued messages, the chat component is not ready");
    return;
  }

  for (auto const& text : texts) {
    auto const event_id = chat_component_->SubmitText(text);
    if (!event_id.has_value()) {
      continue;
    }
    LogMarker("CHAT_MESSAGE_COMMITTED platform=apple event=" +
              std::to_string(*event_id) + " text=" + ToSingleLine(text));
    SaveState();
  }
}

void AppleChatRuntime::DrainPendingPeers() {
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
    LogError("Dropping queued peers, sync is not ready");
    return;
  }

  auto const local_uid = aether_client_->uid();
  for (auto const& text : peers) {
    auto const uid = ae::Uid::FromString(std::string_view{text});
    if (uid.empty()) {
      LogError("CHAT_PEER_UI_INVALID uid_text=" + text);
      continue;
    }
    if (uid == local_uid) {
      LogError("CHAT_PEER_UI_REJECTED_SELF uid=" +
               examples::FormatAetherUid(uid));
      continue;
    }
    chat_component_->AddPeer(uid);
    SaveState();
    LogMarker("CHAT_PEER_UI_ADDED platform=apple uid=" +
              examples::FormatAetherUid(uid));
  }
}

void AppleChatRuntime::PublishTranscript(std::string const& transcript) {
  LogMarker("TRANSCRIPT_PUBLISHED bytes=" +
            std::to_string(transcript.size()));
  if (callbacks_.on_transcript) {
    callbacks_.on_transcript(transcript);
  }
}

void AppleChatRuntime::SaveState() {
  if (!app_.is_valid()) {
    return;
  }
  app_.Save();
  LogMarker("STATE_SAVED");
}

void AppleChatRuntime::WakeUp() {
  auto* scheduler = scheduler_.load(std::memory_order::acquire);
  if (scheduler != nullptr) {
    scheduler->Task([]() {});
  }
}

}  // namespace apptraverse::apple
