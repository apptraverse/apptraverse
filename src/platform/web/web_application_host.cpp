#include "apptraverse/platform/web/web_application_host.h"

#include <vector>

#include <emscripten/threading.h>

#include "apptraverse/noninteractive_crt.h"

namespace apptraverse::web {

WebApplicationHost::WebApplicationHost(
    ApplicationModelRuntime& runtime,
    ui::web::WebPresentationHost& presentation_host,
    WebApplicationHostCallbacks callbacks)
    : runtime_{runtime},
      presentation_host_{presentation_host},
      callbacks_{std::move(callbacks)},
      model_proxy_{[this](ModelObjectProxy::ModelWork work) {
        runtime_.Post(std::move(work));
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

}  // namespace apptraverse::web
