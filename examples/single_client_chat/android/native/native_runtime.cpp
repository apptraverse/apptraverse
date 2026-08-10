#include "native_runtime.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <system_error>
#include <thread>

#include "aether/clock.h"
#include "aether/obj/obj.h"

#include "apptraverse/application_ids.h"
#include "apptraverse/app.h"
#include "apptraverse/chat.h"
#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/window.h"

#include "../../common/graph_builder.h"
#include "android_log.h"
#include "android_window.h"
#include "android_window_presenter.h"

namespace apptraverse::android {
namespace {

APPTRAVERSE_REGISTER(AndroidWindow);
APPTRAVERSE_REGISTER(AndroidWindowPresenter);
APPTRAVERSE_REGISTER(AndroidChatPresenter);

// The core loop wakes up at least this often so a stop request is never missed.
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
    PublishStatus("Native runtime failed to start");
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

  // The model is only touched on the core thread.
  auto* scheduler = scheduler_.load(std::memory_order::acquire);
  if (scheduler != nullptr) {
    scheduler->Task([this]() { DrainPendingSends(); });
  }
  return true;
}

void NativeRuntime::RequestSnapshot() {
  snapshot_requested_.store(true, std::memory_order::release);
  auto* scheduler = scheduler_.load(std::memory_order::acquire);
  if (scheduler != nullptr) {
    scheduler->Task([this]() {
      if (snapshot_requested_.exchange(false, std::memory_order::acq_rel)) {
        PublishSnapshot();
      }
    });
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
  aether_app_ = ae::AetherApp::Construct(ae::AetherAppContext{[storage_root]() {
    return std::make_unique<DirectoryDomainStorage>(
        std::filesystem::path{storage_root});
  }});
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

  if (!LoadPresenters()) {
    return false;
  }

  PublishSnapshot();
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

  auto& window_presenter =
      static_cast<AndroidWindowPresenter&>(*presenter);
  chat_presenter_ = window_presenter.LoadAndroidChatPresenter();
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
  PublishStatus("Native runtime stopped");
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
    PublishStatus("Model is not loaded, message dropped");
    return;
  }

  for (auto const& text : texts) {
    // Journal timestamps must stay unique and monotonic.
    WaitForUniqueTimestamp();
    chat_presenter_->SubmitText(text);
    LogMarker("MESSAGE_COMMITTED text=" + ToSingleLine(text));
    SaveState();
    ui_bridge_.PostMessageCommitted(text);
  }
  chat_presenter_->PublishTranscript();
}

void NativeRuntime::WaitForUniqueTimestamp() {
  auto const& chat = chat_presenter_->chat;
  if (!chat.is_loaded() || chat->journal.empty()) {
    return;
  }
  auto const last_timestamp_us = chat->journal.back().timestamp_us;
  while (SystemUtcMicros() <= last_timestamp_us) {
    std::this_thread::sleep_for(std::chrono::microseconds{1});
  }
}

void NativeRuntime::PublishStatus(std::string const& status) {
  ui_bridge_.PostStatus(status);
}

void NativeRuntime::PublishTranscript(std::string const& transcript) {
  ui_bridge_.PostTranscript(transcript);
  LogMarker("TRANSCRIPT_PUBLISHED bytes=" + std::to_string(transcript.size()) +
            " text=" + ToSingleLine(transcript));
}

void NativeRuntime::PublishSnapshot() {
  PublishStatus("Aether ready, state " + state_dir_);
  if (chat_presenter_ != nullptr) {
    chat_presenter_->PublishTranscript();
  }
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
