#include "apptraverse/platform/windows/win_application_host.h"

#include <vector>

namespace apptraverse {
namespace windows {

WinApplicationHost::WinApplicationHost(ApplicationModelRuntime& runtime,
                                         WinApplicationLoadingOptions loading,
                                         WinApplicationHostCallbacks callbacks)
    : runtime_{runtime},
      callbacks_{std::move(callbacks)},
      model_proxy_{[this](ModelObjectProxy::ModelWork work) {
        runtime_.Post(std::move(work));
      }},
      loading_(loading.class_name, loading.title, loading.message, loading.x,
               loading.y, loading.width, loading.height),
      message_host_(
          [this] {
            OnInitialPublished();
            loading_.Close();
          },
          [this] { OnIncrementalPublished(); },
          [this] {
            if (callbacks_.request_stop) {
              callbacks_.request_stop();
            }
          }) {
  presentation_host_.notify_hwnd = message_host_.notify_hwnd();
  presentation_host_.opaque = this;
  presentation_host_.on_desktop_window_user_close =
      &WinApplicationHost::OnDesktopWindowUserClose;
}

WinApplicationHost::~WinApplicationHost() = default;

void WinApplicationHost::OnInitialPublished() {
  std::vector<std::uint8_t> bytes = runtime_.TakePublishedCopyForUiApply();
  ui::windows::WinPresentationHost* presentation_host = &presentation_host_;
  if (callbacks_.presentation_host_override != nullptr) {
    presentation_host = callbacks_.presentation_host_override;
    presentation_host->notify_hwnd = presentation_host_.notify_hwnd;
    presentation_host->opaque = presentation_host_.opaque;
    presentation_host->on_desktop_window_user_close =
        presentation_host_.on_desktop_window_user_close;
  }
  ui_session_.emplace(bytes, presentation_host, model_proxy_,
                      callbacks_.prepare_ui_presenters);
  if (callbacks_.after_initial) {
    callbacks_.after_initial(ui_session_->root());
  }
  runtime_.NotifyPublicationConsumed();
}

void WinApplicationHost::OnIncrementalPublished() {
  std::vector<std::uint8_t> bytes = runtime_.TakePublishedCopyForUiApply(
      callbacks_.take_incremental_sidecar);
  ui_session_->ApplyStructural(bytes);
  if (callbacks_.after_incremental) {
    callbacks_.after_incremental(ui_session_->root());
  }
  runtime_.NotifyPublicationConsumed();
}

void WinApplicationHost::OnDesktopWindowUserClose(void* opaque,
                                                  ae::Obj* desktop_window) {
  auto* self = static_cast<WinApplicationHost*>(opaque);
  self->callbacks_.on_desktop_window_close(self->ui_session_->root(),
                                           *desktop_window, self->model_proxy_);
}

void WinApplicationHost::Run() {
  message_host_.StartModelThread([this] {
    callbacks_.run_model([this](ApplicationPublicationKind kind) {
      message_host_.NotifyPublished(kind);
    });
  });

  message_host_.PumpUntilModelDone([this] {
    if (!loading_.is_open()) {
      if (callbacks_.request_stop) {
        callbacks_.request_stop();
      }
    }
  });

  message_host_.JoinModelThread();
  ui_session_.reset();
}

}  // namespace windows
}  // namespace apptraverse
