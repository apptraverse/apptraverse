#include "apptraverse/platform/web/web_application_host.h"

#include <cerrno>
#include <utility>
#include <vector>

#include <emscripten.h>
#include <emscripten/atomic.h>
#include <emscripten/threading.h>

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/platform/web/web_idbfs.h"

namespace apptraverse::web {
namespace {

void WaitForWorkerMainTeardownAck(
    std::atomic<std::uint32_t>& worker_main_teardown_done) {
  std::uint32_t expected = 0;
  while (worker_main_teardown_done.load(std::memory_order_acquire) ==
         expected) {
    emscripten_futex_wait(
        reinterpret_cast<volatile void*>(&worker_main_teardown_done), expected,
        1e9);
  }
}

void SignalWorkerMainTeardownDone(
    std::atomic<std::uint32_t>& worker_main_teardown_done) {
  worker_main_teardown_done.store(1, std::memory_order_release);
  emscripten_futex_wake(
      reinterpret_cast<volatile void*>(&worker_main_teardown_done),
      INT_MAX);
}

struct TryJoinHostContext {
  WebApplicationHost* host;
};

void TryJoinTrampoline(void* arg) {
  auto* ctx = static_cast<TryJoinHostContext*>(arg);
  ctx->host->TryJoinModelThreadOnce();
  delete ctx;
}

}  // namespace

void* WebApplicationHost::ModelThreadTrampoline(void* arg) {
  static_cast<WebApplicationHost*>(arg)->RunModelWorkerThread();
  return nullptr;
}

void WebApplicationHost::RunModelWorkerThread() {
  int const session_result =
      callbacks_.run_model(ModelThreadPublicationBridge());
  bool const synced = proxy_queue_.proxySync(
      emscripten_main_runtime_thread_id(), [this, session_result] {
        PrepareShutdownUiOnMainThread(session_result);
      });
  if (!synced) {
    WriteFatalStderr(
        "fatal: WebApplicationHost shutdown UI proxySync failed\n");
    std::abort();
  }
  bool const queued = proxy_queue_.proxyAsync(
      emscripten_main_runtime_thread_id(), [this] {
        CompleteShutdownFlushOnMainThread();
      });
  if (!queued) {
    WriteFatalStderr(
        "fatal: WebApplicationHost shutdown flush proxyAsync failed\n");
    std::abort();
  }
  WaitForWorkerMainTeardownAck(worker_main_teardown_done_);
}

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
  if (model_thread_live_) {
    WriteFatalStderr(
        "fatal: WebApplicationHost destroyed before async shutdown completed\n");
    std::abort();
  }
}

void WebApplicationHost::EnqueueModelWork(ModelObjectProxy::ModelWork work) {
  if (lifecycle_.load(std::memory_order_acquire) !=
      WebApplicationHostLifecycle::kRunning) {
    WriteFatalStderr(
        "fatal: model work posted while WebApplicationHost is stopping\n");
    std::abort();
  }
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
    if (lifecycle_.load(std::memory_order_acquire) ==
        WebApplicationHostLifecycle::kStopping) {
      return;
    }
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
  worker_main_teardown_done_.store(0, std::memory_order_release);
  if (model_thread_live_) {
    WriteFatalStderr("fatal: WebApplicationHost::Start with live model thread\n");
    std::abort();
  }
  int const create_rc =
      pthread_create(&model_thread_, nullptr, ModelThreadTrampoline, this);
  if (create_rc != 0) {
    WriteFatalStderr("fatal: WebApplicationHost pthread_create failed\n");
    std::abort();
  }
  model_thread_live_ = true;
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
  if (lifecycle_.load(std::memory_order_acquire) ==
      WebApplicationHostLifecycle::kStopping) {
    if (shutdown_waiting_for_save_) {
      shutdown_waiting_for_save_ = false;
      BeginModelStopAfterSaveBarrier();
    }
    return;
  }
  if (callbacks_.set_ui_input_blocked) {
    callbacks_.set_ui_input_blocked(false);
  }
  if (callbacks_.on_save_failed) {
    callbacks_.on_save_failed();
  }
}

void WebApplicationHost::FinishSaveSuccess() {
  save_state_.store(SaveState::kIdle, std::memory_order_release);
  if (lifecycle_.load(std::memory_order_acquire) ==
      WebApplicationHostLifecycle::kStopping) {
    if (shutdown_waiting_for_save_) {
      shutdown_waiting_for_save_ = false;
      BeginModelStopAfterSaveBarrier();
    }
    return;
  }
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
  if (lifecycle_.load(std::memory_order_acquire) !=
      WebApplicationHostLifecycle::kRunning) {
    return;
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

void WebApplicationHost::RequestShutdown() {
  if (!emscripten_is_main_browser_thread()) {
    WriteFatalStderr(
        "fatal: WebApplicationHost::RequestShutdown off main thread\n");
    std::abort();
  }
  WebApplicationHostLifecycle expected = WebApplicationHostLifecycle::kRunning;
  if (!lifecycle_.compare_exchange_strong(
          expected, WebApplicationHostLifecycle::kStopping,
          std::memory_order_acq_rel)) {
    return;
  }
  if (callbacks_.set_ui_input_blocked) {
    callbacks_.set_ui_input_blocked(true);
  }
  if (callbacks_.on_shutdown_stopping) {
    callbacks_.on_shutdown_stopping();
  }
  if (save_state_.load(std::memory_order_acquire) == SaveState::kSaving) {
    shutdown_waiting_for_save_ = true;
    return;
  }
  BeginModelStopAfterSaveBarrier();
}

void WebApplicationHost::BeginModelStopAfterSaveBarrier() {
  if (callbacks_.request_stop) {
    callbacks_.request_stop();
  }
}

void WebApplicationHost::PrepareShutdownUiOnMainThread(int persist_result) {
  if (!emscripten_is_main_browser_thread()) {
    WriteFatalStderr(
        "fatal: WebApplicationHost shutdown UI off main thread\n");
    std::abort();
  }
  if (lifecycle_.load(std::memory_order_acquire) !=
      WebApplicationHostLifecycle::kStopping) {
    WriteFatalStderr(
        "fatal: model session ended without WebApplicationHost shutdown\n");
    std::abort();
  }

  shutdown_persist_ok_ = persist_result == 0;
  shutdown_flush_ok_ = false;
  ui_session_.reset();
}

void WebApplicationHost::CompleteShutdownFlushOnMainThread() {
  if (!emscripten_is_main_browser_thread()) {
    WriteFatalStderr(
        "fatal: WebApplicationHost shutdown flush off main thread\n");
    std::abort();
  }
  if (!shutdown_persist_ok_) {
    ReleaseModelWorkerAfterMainTeardown();
    ScheduleTryJoinModelThread();
    return;
  }

  SyncIdbfsStore(
      [this] {
        shutdown_flush_ok_ = true;
        ReleaseModelWorkerAfterMainTeardown();
        ScheduleTryJoinModelThread();
      },
      [this] {
        shutdown_flush_ok_ = false;
        ReleaseModelWorkerAfterMainTeardown();
        ScheduleTryJoinModelThread();
      });
}

void WebApplicationHost::ReleaseModelWorkerAfterMainTeardown() {
  SignalWorkerMainTeardownDone(worker_main_teardown_done_);
}

void WebApplicationHost::ScheduleTryJoinModelThread() {
  auto* ctx = new TryJoinHostContext{this};
  emscripten_async_call(TryJoinTrampoline, ctx, 0);
}

void WebApplicationHost::TryJoinModelThreadOnce() {
  if (!emscripten_is_main_browser_thread()) {
    WriteFatalStderr("fatal: TryJoinModelThreadOnce off main thread\n");
    std::abort();
  }
  if (!model_thread_live_) {
    if (shutdown_persist_ok_ && shutdown_flush_ok_) {
      FinishShutdownSuccess();
    } else {
      FinishShutdownFailure();
    }
    lifecycle_.store(WebApplicationHostLifecycle::kStopped,
                     std::memory_order_release);
    return;
  }
  void* join_result = nullptr;
  int const rc = pthread_tryjoin_np(model_thread_, &join_result);
  if (rc == EBUSY) {
    ScheduleTryJoinModelThread();
    return;
  }
  if (rc != 0) {
    WriteFatalStderr("fatal: pthread_tryjoin_np failed during Web shutdown\n");
    std::abort();
  }
  model_thread_live_ = false;
  model_thread_ = pthread_t{};
  if (shutdown_persist_ok_ && shutdown_flush_ok_) {
    FinishShutdownSuccess();
  } else {
    FinishShutdownFailure();
  }
  lifecycle_.store(WebApplicationHostLifecycle::kStopped,
                   std::memory_order_release);
}

void WebApplicationHost::FinishShutdownSuccess() {
  if (shutdown_failure_reported_) {
    return;
  }
  if (callbacks_.on_shutdown_succeeded) {
    callbacks_.on_shutdown_succeeded();
  }
}

void WebApplicationHost::FinishShutdownFailure() {
  if (shutdown_failure_reported_) {
    return;
  }
  shutdown_failure_reported_ = true;
  if (callbacks_.on_shutdown_failed) {
    callbacks_.on_shutdown_failed();
  }
}

}  // namespace apptraverse::web
