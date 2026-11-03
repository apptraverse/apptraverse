#include "apptraverse/platform/linux/linux_application_host.h"

#include <vector>

#include <glib.h>
#include <gtk/gtk.h>

#include "apptraverse/platform/linux/linux_fatal.h"

namespace apptraverse {
namespace linux {
namespace {

struct PublicationIdlePayload {
  LinuxApplicationHost* host;
  ApplicationPublicationKind kind;
};

}  // namespace

gboolean LinuxApplicationHostPublicationIdle(gpointer data) {
  auto* payload = static_cast<PublicationIdlePayload*>(data);
  payload->host->HandlePublication(payload->kind);
  delete payload;
  return G_SOURCE_REMOVE;
}

LinuxApplicationHost::LinuxApplicationHost(
    ApplicationModelRuntime& runtime, LinuxApplicationHostCallbacks callbacks)
    : runtime_{runtime},
      callbacks_{std::move(callbacks)},
      model_proxy_{[this](ModelObjectProxy::ModelWork work) {
        runtime_.Post(std::move(work));
      }} {
  presentation_host_.opaque = this;
  presentation_host_.on_desktop_window_user_close =
      &LinuxApplicationHost::OnDesktopWindowUserClose;
}

LinuxApplicationHost::~LinuxApplicationHost() {
  if (model_thread_.joinable()) {
    model_thread_.join();
  }
}

void LinuxApplicationHost::SchedulePublication(ApplicationPublicationKind kind) {
  auto* payload = new PublicationIdlePayload{this, kind};
  g_idle_add_full(G_PRIORITY_DEFAULT, &LinuxApplicationHostPublicationIdle,
                  payload, nullptr);
}

void LinuxApplicationHost::HandlePublication(ApplicationPublicationKind kind) {
  if (kind == ApplicationPublicationKind::Initial) {
    OnInitialPublished();
    return;
  }
  OnIncrementalPublished();
}

void LinuxApplicationHost::OnInitialPublished() {
  std::vector<std::uint8_t> bytes = runtime_.TakePublishedCopyForUiApply();
  ui_session_.emplace(bytes, &presentation_host_, model_proxy_);
  if (callbacks_.after_initial) {
    callbacks_.after_initial(ui_session_->root());
  }
  runtime_.NotifyPublicationConsumed();
}

void LinuxApplicationHost::OnIncrementalPublished() {
  if (!ui_session_) {
    FatalLinux("LinuxApplicationHost incremental before initial");
  }
  std::vector<std::uint8_t> bytes = runtime_.TakePublishedCopyForUiApply(
      callbacks_.take_incremental_sidecar);
  ui_session_->ApplyStructural(bytes);
  if (callbacks_.after_incremental) {
    callbacks_.after_incremental(ui_session_->root());
  }
  runtime_.NotifyPublicationConsumed();
}

void LinuxApplicationHost::OnDesktopWindowUserClose(void* opaque,
                                                      ae::Obj* desktop_window) {
  auto* self = static_cast<LinuxApplicationHost*>(opaque);
  self->callbacks_.on_desktop_window_close(self->ui_session_->root(),
                                           *desktop_window, self->model_proxy_);
}

void LinuxApplicationHost::Run() {
  int argc = 0;
  char** argv = nullptr;
  gtk_init(&argc, &argv);

  model_thread_done_.store(false, std::memory_order_release);
  model_thread_ = std::thread([this] {
    callbacks_.run_model([this](ApplicationPublicationKind kind) {
      SchedulePublication(kind);
    });
    model_thread_done_.store(true, std::memory_order_release);
    g_main_context_wakeup(g_main_context_default());
  });

  while (!model_thread_done_.load(std::memory_order_acquire)) {
    g_main_context_iteration(nullptr, TRUE);
  }
  while (g_main_context_pending(nullptr)) {
    g_main_context_iteration(nullptr, FALSE);
  }

  if (model_thread_.joinable()) {
    model_thread_.join();
  }
  ui_session_.reset();
}

}  // namespace linux
}  // namespace apptraverse
