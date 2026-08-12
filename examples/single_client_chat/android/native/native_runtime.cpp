#include "native_runtime.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <system_error>

#include "aether/clock.h"
#include "aether/obj/obj.h"

#include "model/application_ids.h"
#include "model/app.h"
#include "model/chat.h"
#include "model/registration.h"
#include "model/window.h"
#include "apptraverse/directory_domain_storage.h"

#include "../../common/aether_p2p_transport.h"
#include "../../common/aether_runtime.h"
#include "../../common/chat_sync_controller.h"
#include "../../common/graph_builder.h"
#include "android_log.h"
#include "android_system_dns_resolver.h"
#include "android_window.h"
#include "android_window_presenter.h"

#include "model/chat_presenter.h"

namespace apptraverse::android {
namespace {

APPTRAVERSE_REGISTER(AndroidWindow);
APPTRAVERSE_REGISTER(AndroidWindowPresenter);
APPTRAVERSE_REGISTER(AndroidChatPresenter);

using AndroidSystemDnsResolver = ::apptraverse::examples::AndroidSystemDnsResolver;
APPTRAVERSE_REGISTER(AndroidSystemDnsResolver);

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

NativeRuntime::NativeRuntime(std::string state_dir, UiBridge ui_bridge)
    : state_dir_{std::move(state_dir)}, ui_bridge_{std::move(ui_bridge)} {
  LogMarker("APPTRAVERSE_NATIVE_RUNTIME_CREATED state_dir=" + state_dir_);
}

NativeRuntime::~NativeRuntime() = default;

void NativeRuntime::Run() {
  if (!Setup()) {
    LogError("Native runtime failed to start");
    return;
  }

  while (!stop_requested_.load(std::memory_order::acquire)) {
    auto const now = ae::Now();
    auto const next_time = aether_app_->Update(now);
    if (chat_sync_ != nullptr) {
      chat_sync_->Tick(now);
    }
    if (stop_requested_.load(std::memory_order::acquire)) {
      break;
    }
    aether_app_->WaitUntil(std::min(next_time, ae::Now() + kMaxIdleWait));
  }

  Teardown();
}

bool NativeRuntime::QueueSend(std::string text) {
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

bool NativeRuntime::QueueAddPeer(std::string uid) {
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

void NativeRuntime::QueueWindowChanged(std::int32_t width, std::int32_t height,
                                       std::int32_t density_dpi) {
  if (width <= 0 || height <= 0) {
    return;
  }
  {
    auto lock = std::scoped_lock{pending_lock_};
    pending_viewports_.push_back(PendingViewport{width, height, density_dpi});
  }
  auto* scheduler = scheduler_.load(std::memory_order::acquire);
  if (scheduler != nullptr) {
    scheduler->Task([this]() { DrainPendingViewports(); });
  }
}

void NativeRuntime::Stop() {
  stop_requested_.store(true, std::memory_order::release);
  WakeUp();
}

bool NativeRuntime::Setup() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureSingleClientChatRegistration();
  InstallAetherTeleToLogcat();

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
            " app_domain=" + PointerToHex(app_domain) + " aether_root_id=" +
            std::to_string(aether_app_->aether().id().id()) + " app_id=" +
            std::to_string(app_.id().id()) +
            " match=" + (aether_domain == app_domain ? "1" : "0"));
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

  if (chat_presenter_ != nullptr) {
    chat_presenter_->PublishTranscript();
  }
  DrainPendingViewports();
  DrainPendingSends();
  DrainPendingPeers();
  return true;
}

bool NativeRuntime::LoadOrBuildGraph() {
  auto& domain = aether_app_->domain();

  app_ = App::ptr::Declare(ae::CreateWith{domain}.with_id(
      ToObjId(ApplicationObjId::Application)));
  app_.Load();
  if (app_.is_loaded() && app_->window.is_valid()) {
    LogMarker("ANDROID_GRAPH_LOADED");
    LogAppClientReady();
    return true;
  }

  auto graph =
      examples::BuildSingleClientChatGraph<AndroidWindow, AndroidWindowPresenter,
                                           AndroidChatPresenter>(domain,
                                                                "Android");
  app_ = graph.app;
  if (!app_.is_valid()) {
    LogError("Failed to build the single client chat graph");
    return false;
  }
  LogMarker("ANDROID_GRAPH_CREATED");
  SaveState();
  LogAppClientReady();
  return true;
}

bool NativeRuntime::SelectAetherClient() {
  int select_error = 0;
  LogMarker("AETHER_SELECT_CLIENT_START name=" +
            std::string{examples::kAndroidAetherClientName});
  aether_client_ = examples::SelectPersistentAetherClient(
      aether_app_, examples::kAndroidAetherClientName, &select_error);
  if (!aether_client_) {
    LogError("Failed to select Aether client error=" +
             std::to_string(select_error) +
             " exited=" + (aether_app_->IsExited() ? "1" : "0"));
    return false;
  }
  LogMarker("AETHER_CLIENT_READY platform=android uid=" +
            examples::FormatAetherUid(aether_client_->uid()));
  ui_bridge_.PostAetherUid(examples::FormatAetherUid(aether_client_->uid()));
  return true;
}

void NativeRuntime::StartP2pTransport() {
  p2p_transport_ = std::make_unique<examples::AetherP2pTransport>();
  p2p_transport_->SetLogHandler([](std::string line) { LogMarker(line); });
  p2p_transport_->Start(aether_app_, aether_client_);
  LogMarker("AETHER_P2P_TRANSPORT_READY");
}

bool NativeRuntime::StartChatSync() {
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
  auto peer_set = chat->peer_set;
  peer_set.Load();
  if (!peer_set.is_loaded()) {
    LogError("Failed to load ChatPeerSet for sync");
    return false;
  }

  auto system_utc_micros = []() -> std::int64_t {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
  };

  auto sync_send = [this, system_utc_micros](
                       ae::Uid const& peer, ae::ObjId packet_id,
                       SerializedSyncPacket const& bytes) {
    p2p_transport_->Send(peer, bytes);
    LogMarker(
        "SYNC_TRANSPORT_WRITE peer=" + examples::FormatAetherUid(peer) +
        " packet=" + std::to_string(packet_id.id()) +
        " t_us=" + std::to_string(system_utc_micros()));
  };
  auto presence_send = [this](ae::Uid const& peer,
                              std::vector<std::uint8_t> const& bytes) {
    p2p_transport_->Send(peer, bytes);
  };
  auto sync_reconnect = [this](ae::Uid const& peer) {
    p2p_transport_->Reconnect(peer);
  };

  chat_sync_ = std::make_unique<examples::ChatSyncController>(
      SyncReplica{aether_app_->domain(), *domain_storage_, chat.id()}, chat,
      peer_set, sync_send, presence_send, sync_reconnect,
      examples::ChatSyncTiming{}, true,
      [this]() {
        if (chat_presenter_ != nullptr) {
          chat_presenter_->PublishTranscript();
        }
        SaveState();
      },
      [](std::string const& line) { LogMarker(line); });
  chat_sync_->Start();

  p2p_transport_->SetReceiveHandler(
      [this](ae::Uid const& peer, std::vector<std::uint8_t> const& payload) {
        if (examples::TryHandleP2pProbePayload(
                *p2p_transport_, peer, payload,
                [](std::string const& line) { LogMarker(line); })) {
          return;
        }
        if (chat_sync_ != nullptr) {
          chat_sync_->Receive(peer, payload);
        }
      });

  // Dial persisted peers (same as Windows). Without this, Android may keep
  // retrying on a stale pre-outage stream that still reports writable.
  peer_set.Load();
  if (peer_set.is_loaded()) {
    for (auto const& peer : peer_set->peers) {
      if (!peer.remote_uid.empty()) {
        p2p_transport_->Connect(peer.remote_uid);
      }
    }
  }

  LogMarker("CHAT_SYNC_CONTROLLER_READY");
  return true;
}

bool NativeRuntime::LoadPresenters() {
  auto window = app_->window;
  window.Load();
  if (!window.is_loaded()) {
    LogError("Failed to load Window");
    return false;
  }

  auto presenter = window->presenter;
  presenter.Load();
  if (!presenter.is_loaded() ||
      presenter->GetClassId() != AndroidWindowPresenter::kClassId) {
    LogError("Expected AndroidWindowPresenter in the loaded graph");
    return false;
  }

  window_presenter_ = &static_cast<AndroidWindowPresenter&>(*presenter);
  chat_presenter_ = window_presenter_->LoadAndroidChatPresenter();
  if (chat_presenter_ == nullptr) {
    LogError("Expected AndroidChatPresenter in the loaded graph");
    return false;
  }

  chat_presenter_->chat.Load();
  if (!chat_presenter_->chat.is_loaded()) {
    LogError("Failed to load Chat");
    return false;
  }

  chat_presenter_->SetTranscriptPublisher([this](std::string const& text) {
    PublishTranscript(text);
  });
  LogMarker("ANDROID_PRESENTERS_LOADED");
  return true;
}

void NativeRuntime::Teardown() {
  if (chat_sync_ != nullptr) {
    chat_sync_->Stop();
  }
  if (chat_presenter_ != nullptr) {
    SaveState();
    chat_presenter_ = nullptr;
  }
  window_presenter_ = nullptr;
  chat_sync_.reset();
  p2p_transport_.reset();
  aether_client_.Reset();
  domain_storage_ = nullptr;
  scheduler_.store(nullptr, std::memory_order::release);
  app_.Reset();
  aether_app_.Reset();
}

void NativeRuntime::DrainPendingSends() {
  auto texts = std::vector<std::string>{};
  {
    auto lock = std::scoped_lock{pending_lock_};
    texts.swap(pending_sends_);
  }
  if (texts.empty()) {
    return;
  }
  if (chat_presenter_ == nullptr) {
    LogError("Dropping queued messages, the model is not loaded");
    return;
  }

  for (auto const& text : texts) {
    chat_presenter_->SubmitText(text);
    std::uint32_t event_id = 0;
    auto chat = chat_presenter_->chat;
    chat.Load();
    if (chat.is_loaded() && !chat->journal.empty()) {
      event_id = chat->journal.back().event.id().id();
    }
    auto const t_us = std::chrono::duration_cast<std::chrono::microseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
    LogMarker("CHAT_MESSAGE_COMMITTED platform=android event=" +
              std::to_string(event_id) + " text_key=" + ToSingleLine(text) +
              " t_us=" + std::to_string(t_us));
    LogMarker("MESSAGE_COMMITTED text=" + ToSingleLine(text));
    LogJournalSizes();
    SaveState();
  }
  chat_presenter_->PublishTranscript();
}

void NativeRuntime::DrainPendingPeers() {
  auto peers = std::vector<std::string>{};
  {
    auto lock = std::scoped_lock{pending_lock_};
    peers.swap(pending_peers_);
  }
  if (peers.empty()) {
    return;
  }
  if (chat_sync_ == nullptr || p2p_transport_ == nullptr ||
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
    chat_sync_->AddPeer(uid);
    p2p_transport_->Connect(uid);
    SaveState();
    LogMarker("CHAT_PEER_UI_ADDED platform=android uid=" +
              examples::FormatAetherUid(uid));
  }
}

void NativeRuntime::DrainPendingViewports() {
  auto viewports = std::vector<PendingViewport>{};
  {
    auto lock = std::scoped_lock{pending_lock_};
    viewports.swap(pending_viewports_);
  }
  if (viewports.empty()) {
    return;
  }
  if (window_presenter_ == nullptr) {
    LogError("Dropping viewport events, the window presenter is not loaded");
    return;
  }

  for (auto const& viewport : viewports) {
    window_presenter_->CommitViewport(viewport.width, viewport.height,
                                      viewport.density_dpi);
    LogMarker("WINDOW_CHANGED width=" + std::to_string(viewport.width) +
              " height=" + std::to_string(viewport.height) +
              " dpi=" + std::to_string(viewport.density_dpi));
    LogJournalSizes();
    SaveState();
  }
}

void NativeRuntime::PublishTranscript(std::string const& transcript) {
  ui_bridge_.PostTranscript(transcript);
  auto const t_us = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();
  std::size_t start = 0;
  while (start < transcript.size()) {
    auto const end = transcript.find('\n', start);
    auto const line = transcript.substr(
        start, end == std::string::npos ? std::string::npos : end - start);
    start = end == std::string::npos ? transcript.size() : end + 1;
    auto const sep = line.rfind(": ");
    if (sep == std::string::npos) {
      continue;
    }
    auto const key = line.substr(sep + 2);
    if (key.empty() || key.find(' ') != std::string::npos || key.size() > 64) {
      continue;
    }
    if (!visible_message_keys_.insert(key).second) {
      continue;
    }
    LogMarker("CHAT_MESSAGE_VISIBLE platform=android text_key=" + key +
              " t_us=" + std::to_string(t_us));
  }
  LogMarker("TRANSCRIPT_PUBLISHED bytes=" + std::to_string(transcript.size()) +
            " text=" + ToSingleLine(transcript));
  LogJournalSizes();
}

void NativeRuntime::LogJournalSizes() {
  if (app_.is_valid() && app_->window.is_valid()) {
    auto window = app_->window;
    window.Load();
    if (window.is_loaded()) {
      LogMarker("WINDOW_JOURNAL_SIZE n=" +
                std::to_string(window->journal.size()));
    }
  }
  if (chat_presenter_ == nullptr || !chat_presenter_->chat.is_valid()) {
    return;
  }
  auto chat = chat_presenter_->chat;
  chat.Load();
  if (!chat.is_loaded()) {
    return;
  }
  LogMarker("CHAT_JOURNAL_SIZE n=" + std::to_string(chat->journal.size()));
}

void NativeRuntime::LogAppClientReady() {
  if (!app_.is_valid() || !app_.is_loaded()) {
    return;
  }
  auto local_client = app_->local_client;
  if (!local_client.is_valid()) {
    LogError("App.local_client missing");
    return;
  }
  local_client.Load();
  if (!local_client.is_loaded()) {
    LogError("Failed to load App.local_client");
    return;
  }
  LogMarker("APP_CLIENT_READY platform=android obj_id=" +
            std::to_string(local_client.id().id()) +
            " name=" + local_client->name);
}

void NativeRuntime::SaveState() {
  if (!app_.is_valid()) {
    return;
  }
  app_.Save();
  LogMarker("STATE_SAVED");
}

void NativeRuntime::WakeUp() {
  auto* scheduler = scheduler_.load(std::memory_order::acquire);
  if (scheduler != nullptr) {
    scheduler->Task([]() {});
  }
}

}  // namespace apptraverse::android
