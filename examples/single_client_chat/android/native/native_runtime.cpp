#include "native_runtime.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <system_error>

#include "aether/clock.h"
#include "aether/obj/obj.h"

#include "apptraverse/application_ids.h"
#include "apptraverse/app.h"
#include "apptraverse/chat.h"
#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/window.h"

#include "../../common/aether_runtime.h"
#include "../../common/graph_builder.h"
#include "android_log.h"
#include "android_window.h"
#include "android_window_presenter.h"

namespace apptraverse::android {
namespace {

APPTRAVERSE_REGISTER(AndroidWindow);
APPTRAVERSE_REGISTER(AndroidWindowPresenter);
APPTRAVERSE_REGISTER(AndroidChatPresenter);

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
    auto const next_time = aether_app_->Update(ae::Now());
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

  auto ec = std::error_code{};
  std::filesystem::create_directories(std::filesystem::path{state_dir_}, ec);
  if (ec) {
    LogError("Failed to create the state directory " + state_dir_);
    return false;
  }

  auto storage_root = state_dir_;
  aether_app_ = examples::ConstructAetherAppWithEthernet([storage_root]() {
    return std::make_unique<DirectoryDomainStorage>(
        std::filesystem::path{storage_root});
  });
  if (aether_app_.get() == nullptr) {
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

  if (!LoadPresenters()) {
    return false;
  }

  if (chat_presenter_ != nullptr) {
    chat_presenter_->PublishTranscript();
  }
  DrainPendingViewports();
  DrainPendingSends();
  return true;
}

bool NativeRuntime::LoadOrBuildGraph() {
  auto& domain = aether_app_->domain();

  app_ = App::ptr::Declare(ae::CreateWith{domain}.with_id(
      ToObjId(ApplicationObjId::Application)));
  app_.Load();
  if (app_.is_loaded() && app_->window.is_valid()) {
    LogMarker("ANDROID_GRAPH_LOADED");
    return true;
  }

  auto graph =
      examples::BuildSingleClientChatGraph<AndroidWindow, AndroidWindowPresenter,
                                           AndroidChatPresenter>(domain);
  app_ = graph.app;
  if (!app_.is_valid()) {
    LogError("Failed to build the single client chat graph");
    return false;
  }
  LogMarker("ANDROID_GRAPH_CREATED");
  SaveState();
  return true;
}

bool NativeRuntime::SelectAetherClient() {
  aether_client_ = examples::SelectPersistentAetherClient(
      aether_app_, examples::kAndroidAetherClientName);
  if (!aether_client_) {
    LogError("Failed to select Aether client");
    return false;
  }
  LogMarker("AETHER_CLIENT_READY platform=android uid=" +
            examples::FormatAetherUid(aether_client_->uid()));
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
  if (chat_presenter_ != nullptr) {
    SaveState();
    chat_presenter_ = nullptr;
  }
  window_presenter_ = nullptr;
  aether_client_.Reset();
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
    LogMarker("MESSAGE_COMMITTED text=" + ToSingleLine(text));
    LogJournalSizes();
    SaveState();
  }
  chat_presenter_->PublishTranscript();
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
