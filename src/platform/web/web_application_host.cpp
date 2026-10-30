#include "apptraverse/platform/web/web_application_host.h"

#include <utility>
#include <vector>

#include <emscripten/threading.h>

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/platform/web/web_idbfs.h"

namespace apptraverse::web {

WebApplicationHost::WebApplicationHost(
    ApplicationModelRuntime& runtime,
    ui::web::WebPresentationHost& presentation_host,
    WebApplicationHostCallbacks callbacks)
    : runtime_{runtime},
      presentation_host_{presentation_host},
      callbacks_{std::move(callbacks)},
      model_proxy_{[this](ModelObjectProxy::ModelWork work) {
        EnqueueModelWork(std::move(work));
      }} {}

WebApplicationHost::~WebApplicationHost() {
  if (callbacks_.request_stop) {
    callbacks_.request_stop();
  }
  if (model_thread_.joinable()) {
    model_thread_.join();
  }
  ui_session_.reset();
}

void WebApplicationHost::EnqueueModelWork(ModelObjectProxy::ModelWork work) {
  if (save_state_.load(std::memory_order_acquire) == SaveState::kSaving) {
    WriteFatalStderr(
        "fatal: model work posted while WebApplicationHost save in progress\n");
    std::abort();
  }
  runtime_.Post(std::move(work));
}

ApplicationPublishedCallback WebApplicationHost::ModelThreadPublicationBridge() {
  return [this](ApplicationPublicationKind kind) {
    bool const queued = proxy_queue_.proxyAsync(
        emscripten_main_runtime_thread_id(),
        [this, kind] { HandlePublicationOnMainThread(kind); });
    if (!queued) {
      WriteFatalStderr("fatal: WebApplicationHost proxyAsync failed\n");
      std::abort();
    }
  };
}

void WebApplicationHost::HandlePublicationOnMainThread(
    ApplicationPublicationKind kind) {
  if (!emscripten_is_main_browser_thread()) {
    WriteFatalStderr("fatal: WebApplicationHost publication off main thread\n");
    std::abort();
  }
  if (kind == ApplicationPublicationKind::Initial) {
    if (callbacks_.refresh_presentation_host) {
      callbacks_.refresh_presentation_host(presentation_host_);
    }
    if (presentation_host_.mount == nullptr) {
      WriteFatalStderr(
          "fatal: WebPresentationHost mount missing before UI session\n");
      std::abort();
    }
    std::vector<std::uint8_t> bytes = runtime_.TakePublishedCopy();
    runtime_.NotifyPublicationConsumed();
    ui_session_.emplace(bytes, &presentation_host_, model_proxy_);
    if (callbacks_.after_initial) {
      callbacks_.after_initial(ui_session_->root());
    }
    return;
  }
  if (!ui_session_) {
    WriteFatalStderr("fatal: incremental publication before initial\n");
    std::abort();
  }
  std::vector<std::uint8_t> bytes =
      runtime_.TakePublishedCopy(callbacks_.take_incremental_sidecar);
  runtime_.NotifyPublicationConsumed();
  ui_session_->ApplyStructural(bytes);
  if (callbacks_.after_incremental) {
    callbacks_.after_incremental(ui_session_->root());
  }
}

void WebApplicationHost::Start() {
  if (!emscripten_is_main_browser_thread()) {
    WriteFatalStderr("fatal: WebApplicationHost::Start off main thread\n");
    std::abort();
  }
  model_thread_ = std::thread([this] {
    callbacks_.run_model(ModelThreadPublicationBridge());
  });
}

void WebApplicationHost::StartWithPersistentStorage(
    std::string idbfs_mount_path) {
  if (!emscripten_is_main_browser_thread()) {
    WriteFatalStderr(
        "fatal: WebApplicationHost::StartWithPersistentStorage off main thread\n");
    std::abort();
  }
  idbfs_mount_path_ = std::move(idbfs_mount_path);
  MountIdbfsAndSyncLoad(
      idbfs_mount_path_,
      [this] { Start(); },
      [] {
        WriteFatalStderr("fatal: IDBFS syncfs(true) populate failed\n");
        std::abort();
      });
}

void WebApplicationHost::FinishSaveFailure() {
  save_state_.store(SaveState::kIdle, std::memory_order_release);
  if (callbacks_.set_ui_input_blocked) {
    callbacks_.set_ui_input_blocked(false);
  }
  if (callbacks_.on_save_failed) {
    callbacks_.on_save_failed();
  }
}

void WebApplicationHost::FinishSaveSuccess() {
  save_state_.store(SaveState::kIdle, std::memory_order_release);
  if (callbacks_.set_ui_input_blocked) {
    callbacks_.set_ui_input_blocked(false);
  }
  if (callbacks_.on_save_succeeded) {
    callbacks_.on_save_succeeded();
  }
}

void WebApplicationHost::RequestSave() {
  if (!emscripten_is_main_browser_thread()) {
    WriteFatalStderr("fatal: WebApplicationHost::RequestSave off main thread\n");
    std::abort();
  }
  if (!callbacks_.persist_model) {
    WriteFatalStderr("fatal: WebApplicationHost::RequestSave without persist_model\n");
    std::abort();
  }
  SaveState expected = SaveState::kIdle;
  if (!save_state_.compare_exchange_strong(expected, SaveState::kSaving,
                                         std::memory_order_acq_rel)) {
    return;
  }
  if (callbacks_.set_ui_input_blocked) {
    callbacks_.set_ui_input_blocked(true);
  }
  if (callbacks_.on_save_started) {
    callbacks_.on_save_started();
  }
  runtime_.Post([this](ae::Domain& domain) {
    int const persist_result = callbacks_.persist_model(domain);
    bool const queued = proxy_queue_.proxyAsync(
        emscripten_main_runtime_thread_id(), [this, persist_result] {
          if (persist_result != 0) {
            FinishSaveFailure();
            return;
          }
          SyncIdbfsStore([this] { FinishSaveSuccess(); },
                         [this] { FinishSaveFailure(); });
        });
    if (!queued) {
      WriteFatalStderr("fatal: WebApplicationHost save proxyAsync failed\n");
      std::abort();
    }
  });
}

}  // namespace apptraverse::web
