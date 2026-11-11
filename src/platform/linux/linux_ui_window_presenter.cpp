#include "apptraverse/platform/linux/linux_ui_window_presenter.h"

#include <string>

#include "apptraverse/platform/linux/linux_fatal.h"
#include "apptraverse/platform/linux/linux_presentation_host.h"
#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_localization_string.h"

namespace apptraverse::ui::linux {

GtkWidget* LinuxDesktopWindowPresenter::ClientMount() const {
  return client_mount_;
}

void* LinuxDesktopWindowPresenter::NativeChildParentHandle() const {
  return client_mount_;
}

std::string LinuxDesktopWindowPresenter::ResolveTitleText() const {
  return ResolvePresentedWindowTitle(locale->language_index());
}

bool LinuxDesktopWindowPresenter::FramesEqual(ClientFrame a, ClientFrame b) {
  return a.x == b.x && a.y == b.y && a.client_width == b.client_width &&
         a.client_height == b.client_height;
}

LinuxDesktopWindowPresenter::ClientFrame
LinuxDesktopWindowPresenter::ReadMirrorClientFrame() const {
  auto const desktop = desktop_window();
  return {desktop->x(), desktop->y(), desktop->restore_client_width(),
          desktop->restore_client_height()};
}

LinuxDesktopWindowPresenter::ClientFrame
LinuxDesktopWindowPresenter::ReadNativeClientFrame() const {
  ClientFrame frame;
  // GTK returns the gravity-relative frame position accepted by
  // gtk_window_move. Keep it independent of the requested position, including
  // when the window manager chooses an initial position or moves it offscreen.
  gtk_window_get_position(GTK_WINDOW(window_), &frame.x, &frame.y);
  GtkAllocation allocation;
  gtk_widget_get_allocation(client_mount_, &allocation);
  frame.client_width = allocation.width;
  frame.client_height = allocation.height;
  return frame;
}

void LinuxDesktopWindowPresenter::HandleNativeFrameChanged() {
  if (applying_mirror_frame_ || user_close_requested_ || !mapped_) {
    return;
  }
  ClientFrame const native = ReadNativeClientFrame();
  if (native.client_width <= 0 || native.client_height <= 0) {
    return;
  }
  GdkWindowState const state =
      gdk_window_get_state(gtk_widget_get_window(window_));
  bool const restore =
      (state & (GDK_WINDOW_STATE_MAXIMIZED | GDK_WINDOW_STATE_ICONIFIED |
                GDK_WINDOW_STATE_FULLSCREEN)) == 0;
  ReportNativeFrameToModel(native.x, native.y, native.client_width,
                           native.client_height, restore);
}

void LinuxDesktopWindowPresenter::ReportNativeClientFrame() {
  HandleNativeFrameChanged();
}

gboolean LinuxDesktopWindowPresenter::OnConfigure(GtkWidget* widget,
                                                  GdkEventConfigure* event,
                                                  gpointer user_data) {
  (void)widget;
  (void)event;
  static_cast<LinuxDesktopWindowPresenter*>(user_data)->HandleNativeFrameChanged();
  return FALSE;
}

gboolean LinuxDesktopWindowPresenter::OnMapEvent(GtkWidget* widget,
                                                GdkEvent* event,
                                                gpointer user_data) {
  (void)event;
  auto* self = static_cast<LinuxDesktopWindowPresenter*>(user_data);
  bool const initial_map = !self->mapped_;
  self->mapped_ = true;
  self->HandleNativeFrameChanged();
  if (initial_map) {
    GdkMonitor* const monitor = gdk_display_get_monitor_at_window(
        gtk_widget_get_display(widget), gtk_widget_get_window(widget));
    if (monitor == nullptr) {
      FatalLinux("gdk_display_get_monitor_at_window");
    }
    GdkRectangle workarea;
    gdk_monitor_get_workarea(monitor, &workarea);
    self->SubmitScreenFromNative(workarea.x, workarea.y, workarea.width,
                                  workarea.height);
  }
  return FALSE;
}

void LinuxDesktopWindowPresenter::OnClientMountSizeAllocate(
    GtkWidget* widget, GdkRectangle* allocation, gpointer user_data) {
  (void)widget;
  (void)allocation;
  static_cast<LinuxDesktopWindowPresenter*>(user_data)->HandleNativeFrameChanged();
}

gboolean LinuxDesktopWindowPresenter::OnDeleteEvent(GtkWidget* widget,
                                                   GdkEvent* event,
                                                   gpointer user_data) {
  (void)widget;
  (void)event;
  auto* self = static_cast<LinuxDesktopWindowPresenter*>(user_data);
  if (self->user_close_requested_) {
    return TRUE;
  }
  self->HandleNativeFrameChanged();
  // Model Close may unload the target before the publication destroys GTK UI.
  self->user_close_requested_ = true;
  self->DisconnectNativeFrameReports();
  auto* host = static_cast<LinuxPresentationHost*>(self->presentation_host);
  host->on_desktop_window_user_close(host->opaque, self->window.operator->());
  return TRUE;
}

void LinuxDesktopWindowPresenter::ApplyRequestedFrameFromMirror() {
  if (applying_mirror_frame_ || user_close_requested_ || !mapped_) {
    return;
  }
  ClientFrame const mirror = ReadMirrorClientFrame();
  ClientFrame const native = ReadNativeClientFrame();
  if (FramesEqual(mirror, native)) {
    return;
  }
  applying_mirror_frame_ = true;
  gtk_window_resize(GTK_WINDOW(window_), mirror.client_width,
                    mirror.client_height);
  gtk_window_move(GTK_WINDOW(window_), mirror.x, mirror.y);
  applying_mirror_frame_ = false;
}

void LinuxDesktopWindowPresenter::OnLoad() {
  auto const desktop = desktop_window();
  user_close_requested_ = false;
  mapped_ = false;

  window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  if (window_ == nullptr) {
    FatalLinux("gtk_window_new");
  }
  g_object_set_data(G_OBJECT(window_), "apptraverse_presenter", this);
  delete_handler_ = g_signal_connect(window_, "delete-event",
                                     G_CALLBACK(OnDeleteEvent), this);
  map_handler_ =
      g_signal_connect(window_, "map-event", G_CALLBACK(OnMapEvent), this);
  configure_handler_ = g_signal_connect(window_, "configure-event",
                                        G_CALLBACK(OnConfigure), this);

  client_mount_ = gtk_fixed_new();
  if (client_mount_ == nullptr) {
    FatalLinux("gtk_fixed_new");
  }
  gtk_container_add(GTK_CONTAINER(window_), client_mount_);
  size_allocate_handler_ =
      g_signal_connect(client_mount_, "size-allocate",
                       G_CALLBACK(OnClientMountSizeAllocate), this);

  applying_mirror_frame_ = true;
  gtk_window_set_default_size(GTK_WINDOW(window_),
                               desktop->restore_client_width(),
                               desktop->restore_client_height());
  if (desktop->HasExplicitPosition()) {
    gtk_window_move(GTK_WINDOW(window_), desktop->x(), desktop->y());
    gtk_window_set_geometry_hints(GTK_WINDOW(window_), nullptr, nullptr,
                                  GDK_HINT_USER_POS);
  }
  applying_mirror_frame_ = false;
  SyncTitleFromMirror();
  gtk_widget_show_all(window_);
}

void LinuxDesktopWindowPresenter::SyncTitleFromMirror() {
  std::string const title_text = ResolveTitleText();
  gtk_window_set_title(GTK_WINDOW(window_), title_text.c_str());
}

void LinuxDesktopWindowPresenter::OnModelChanged() {
  SyncTitleFromMirror();
}

void LinuxDesktopWindowPresenter::DisconnectNativeFrameReports() {
  if (configure_handler_ != 0) {
    g_signal_handler_disconnect(window_, configure_handler_);
    configure_handler_ = 0;
  }
  if (size_allocate_handler_ != 0) {
    g_signal_handler_disconnect(client_mount_, size_allocate_handler_);
    size_allocate_handler_ = 0;
  }
}

void LinuxDesktopWindowPresenter::OnUnload() {
  g_signal_handler_disconnect(window_, delete_handler_);
  g_signal_handler_disconnect(window_, map_handler_);
  delete_handler_ = 0;
  map_handler_ = 0;
  DisconnectNativeFrameReports();
  gtk_widget_destroy(window_);
  window_ = nullptr;
  client_mount_ = nullptr;
  mapped_ = false;
}

}  // namespace apptraverse::ui::linux
