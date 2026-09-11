#include "linux_app.h"

#include <utility>

#include "apptraverse/object_serialization.h"
#include "linux_fatal.h"
#include "linux_presenters.h"
#include "surfaces_linux_messages.h"

namespace apptraverse {
namespace {

struct WakePayload {
  LinuxApp* app;
  std::uint8_t code;
};

struct GuiInvokePayload {
  std::function<void()> fn;
};

}  // namespace

gboolean LinuxAppWakeIdle(gpointer data) {
  auto* payload = static_cast<WakePayload*>(data);
  payload->app->HandleWake(payload->code);
  delete payload;
  return G_SOURCE_REMOVE;
}

gboolean LinuxAppGuiInvokeIdle(gpointer data) {
  auto* payload = static_cast<GuiInvokePayload*>(data);
  payload->fn();
  delete payload;
  return G_SOURCE_REMOVE;
}

void LinuxApp::InvokeOnGui(std::function<void()> fn) {
  auto* payload = new GuiInvokePayload{std::move(fn)};
  gdk_threads_add_idle(&LinuxAppGuiInvokeIdle, payload);
}

void LinuxApp::PostWake(std::uint8_t code) {
  auto* payload = new WakePayload{this, code};
  gdk_threads_add_idle(&LinuxAppWakeIdle, payload);
}

void LinuxApp::RegisterPresenter(GtkWidget* window,
                                 LinuxSurfacePresenter* presenter) {
  presenters_[window] = presenter;
}

void LinuxApp::UnregisterPresenter(GtkWidget* window) {
  presenters_.erase(window);
}

LinuxSurfacePresenter* LinuxApp::PresenterFor(GtkWidget* window) const {
  auto const it = presenters_.find(window);
  if (it == presenters_.end()) {
    return nullptr;
  }
  return it->second;
}

void LinuxApp::ReadOuterBounds(GtkWidget* window, int* x, int* y, int* width,
                               int* height) const {
  gint gx = 0;
  gint gy = 0;
  gint gw = 0;
  gint gh = 0;
  gtk_window_get_position(GTK_WINDOW(window), &gx, &gy);
  gtk_window_get_size(GTK_WINDOW(window), &gw, &gh);
  *x = gx;
  *y = gy;
  *width = gw;
  *height = gh;
}

void LinuxApp::PlaceOuterWindow(GtkWidget* window, int x, int y, int width,
                                int height) {
  if (width > 0 && height > 0) {
    gtk_window_resize(GTK_WINDOW(window), width, height);
  }
  // Best-effort; Wayland compositors may ignore absolute position.
  gtk_window_move(GTK_WINDOW(window), x, y);
}

void LinuxApp::QueueAllWindowBounds() {
  for (auto const& surface : ui_application_->surfaces->surfaces) {
    LinuxSurfacePresenter::ptr presenter{surface->presenter};
    presenter->QueueCurrentBounds();
  }
}

void LinuxApp::QueueFocusedAsCurrent() {
  for (auto const& entry : presenters_) {
    if (gtk_window_has_toplevel_focus(GTK_WINDOW(entry.first))) {
      entry.second->PageShown();
      return;
    }
  }
}

void LinuxApp::RestoreActiveSurfaceZOrder() {
  auto& surfaces = ui_application_->surfaces->surfaces;
  Surface::ptr target = ui_application_->surfaces->mobile_current;
  if (!target) {
    if (surfaces.empty()) {
      return;
    }
    target = surfaces.back();
  }
  LinuxSurfacePresenter::ptr presenter{target->presenter};
  gtk_window_present(GTK_WINDOW(presenter->window));
  presenter->PageShown();
}

void LinuxApp::RequestApplicationStop() {
  if (ui_application_) {
    QueueFocusedAsCurrent();
    QueueAllWindowBounds();
  }
  session_.RequestStop();
}

void LinuxApp::PostApplicationStop() { PostWake(kLinuxWakeStop); }

void LinuxApp::CreateLoadingWindow() {
  loading_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(loading_), "Loading");
  gtk_window_set_default_size(GTK_WINDOW(loading_), 280, 120);
  gtk_window_set_position(GTK_WINDOW(loading_), GTK_WIN_POS_CENTER);
  GtkWidget* label = gtk_label_new("Loading");
  gtk_container_add(GTK_CONTAINER(loading_), label);
  g_signal_connect(loading_, "delete-event",
                   G_CALLBACK(+[](GtkWidget*, GdkEvent*, gpointer) -> gboolean {
                     return TRUE;
                   }),
                   nullptr);
  gtk_widget_show_all(loading_);
}

void LinuxApp::DestroyLoadingWindow() {
  if (loading_ == nullptr) {
    return;
  }
  gtk_widget_destroy(loading_);
  loading_ = nullptr;
}

void LinuxApp::OnInitialPublished() {
  std::vector<std::uint8_t> bytes;
  {
    std::lock_guard<std::mutex> lock{session_.mu};
    bytes = session_.channel.TakePublishedCopy();
  }
  session_.cv.notify_all();
  ui_domain_ = std::make_unique<ae::Domain>(ui_storage_);
  ByteSource in;
  in.data = bytes.data();
  in.size = bytes.size();
  auto ui_root = LoadInitialPublication(in, *ui_domain_, ui_storage_);
  ui_application_ = Application::ptr::MakeFromThis(
      static_cast<Application*>(ui_root.get()));
  InitializePresenters(*ui_application_, this, &*model_proxy_);
  RestoreActiveSurfaceZOrder();
  DestroyLoadingWindow();
}

void LinuxApp::OnIncrementalPublished() {
  std::vector<std::uint8_t> bytes;
  {
    std::lock_guard<std::mutex> lock{session_.mu};
    bytes = session_.channel.TakePublishedCopy();
  }
  session_.cv.notify_all();
  ApplySurfacesStructural(bytes, *ui_application_, ui_storage_, this,
                          &*model_proxy_);
}

void LinuxApp::OnModelFinished() {
  if (ui_application_) {
    UnloadPresenters(*ui_application_);
  }
  ui_application_ = {};
  ui_domain_.reset();
  model_proxy_.reset();
  DestroyLoadingWindow();
  gtk_main_quit();
}

void LinuxApp::HandleWake(std::uint8_t code) {
  if (code == kLinuxWakeInitialPublished) {
    OnInitialPublished();
  } else if (code == kLinuxWakeIncrementalPublished) {
    OnIncrementalPublished();
  } else if (code == kLinuxWakeStop) {
    RequestApplicationStop();
  } else if (code == kLinuxWakeModelFinished) {
    OnModelFinished();
  }
}

int LinuxApp::Run(std::filesystem::path const& state_dir) {
  gtk_init(nullptr, nullptr);
  EnsureLinuxSurfacePresenterRegistration();

  session_.state_dir = state_dir;
  model_proxy_.emplace([this](ModelObjectProxy::ModelWork work) {
    session_.Post(std::move(work));
  });

  CreateLoadingWindow();

  model_done_.store(false);
  model_thread_ = std::thread([this] {
    session_.Run([this](SurfacesPublicationKind kind) {
      PostWake(kind == SurfacesPublicationKind::Initial
                   ? kLinuxWakeInitialPublished
                   : kLinuxWakeIncrementalPublished);
    });
    model_done_.store(true);
    PostWake(kLinuxWakeModelFinished);
  });

  gtk_main();
  model_thread_.join();
  return 0;
}

}  // namespace apptraverse
