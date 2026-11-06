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
  if (!title.is_valid()) {
    return {};
  }
  return title->Resolve(locale->language_index());
}

bool LinuxDesktopWindowPresenter::ClientRectOnMonitor(GtkWidget* window,
                                                      gint origin_x,
                                                      gint origin_y) {
  GdkRectangle const client{origin_x, origin_y,
                            gtk_widget_get_allocated_width(window),
                            gtk_widget_get_allocated_height(window)};
  GdkDisplay* const display = gtk_widget_get_display(window);
  int const monitors = gdk_display_get_n_monitors(display);
  for (int i = 0; i < monitors; ++i) {
    GdkRectangle geometry{};
    gdk_monitor_get_geometry(gdk_display_get_monitor(display, i), &geometry);
    if (gdk_rectangle_intersect(&client, &geometry, nullptr)) {
      return true;
    }
  }
  return false;
}

bool LinuxDesktopWindowPresenter::FramesEqual(ClientFrame a, ClientFrame b) {
  return a.x == b.x && a.y == b.y && a.client_width == b.client_width &&
         a.client_height == b.client_height;
}

LinuxDesktopWindowPresenter::ClientFrame
LinuxDesktopWindowPresenter::ReadMirrorClientFrame() const {
  ClientFrame frame{};
  auto const desktop = desktop_window();
  if (!desktop) {
    return frame;
  }
  frame.x = desktop->x();
  frame.y = desktop->y();
  frame.client_width = desktop->client_width();
  frame.client_height = desktop->client_height();
  return frame;
}

LinuxDesktopWindowPresenter::ClientFrame
LinuxDesktopWindowPresenter::ReadNativeClientFrame() const {
  ClientFrame frame{};
  if (!placement_known_ || window_ == nullptr ||
      !gtk_widget_get_realized(window_)) {
    return frame;
  }
  gint origin_x = 0;
  gint origin_y = 0;
  gdk_window_get_origin(gtk_widget_get_window(window_), &origin_x, &origin_y);
  frame.x = origin_x - placement_dx_;
  frame.y = origin_y - placement_dy_;
  if (client_mount_ != nullptr && gtk_widget_get_visible(client_mount_)) {
    GtkAllocation allocation{};
    gtk_widget_get_allocation(client_mount_, &allocation);
    frame.client_width = allocation.width;
    frame.client_height = allocation.height;
  } else {
    gtk_window_get_size(GTK_WINDOW(window_), &frame.client_width,
                        &frame.client_height);
  }
  return frame;
}

void LinuxDesktopWindowPresenter::HandleNativeFrameChanged() {
  if (applying_mirror_frame_ || user_close_requested_ || !placement_known_) {
    return;
  }
  auto const desktop = desktop_window();
  if (window_ == nullptr || !desktop) {
    return;
  }
  ClientFrame const native = ReadNativeClientFrame();
  if (native.client_width <= 0 || native.client_height <= 0) {
    return;
  }
  SubmitFrameFromNative(native.x, native.y, native.client_width,
                        native.client_height);
}

void LinuxDesktopWindowPresenter::ReportNativeClientFrame() {
  HandleNativeFrameChanged();
}

gboolean LinuxDesktopWindowPresenter::OnConfigure(GtkWidget* widget,
                                                    GdkEventConfigure* event,
                                                    gpointer user_data) {
  (void)event;
  auto* self = static_cast<LinuxDesktopWindowPresenter*>(user_data);
  if (!self->placement_known_) {
    if (!self->mapped_) {
      return FALSE;
    }
    // Some window managers place the client at an offset from the
    // gtk_window_move reference point that _NET_FRAME_EXTENTS does not
    // describe (WSLg adds an undeclared shadow margin). Frames are reported in
    // move coordinates using the offset of the first placement, measured
    // against the last position requested before it.
    gint origin_x = 0;
    gint origin_y = 0;
    gdk_window_get_origin(gtk_widget_get_window(widget), &origin_x, &origin_y);
    if (!ClientRectOnMonitor(widget, origin_x, origin_y)) {
      return FALSE;
    }
    self->placement_dx_ = origin_x - self->placement_request_x_;
    self->placement_dy_ = origin_y - self->placement_request_y_;
    self->placement_known_ = true;
  }
  self->HandleNativeFrameChanged();
  return FALSE;
}

gboolean LinuxDesktopWindowPresenter::OnMapEvent(GtkWidget* widget,
                                                 GdkEvent* event,
                                                 gpointer user_data) {
  (void)widget;
  (void)event;
  static_cast<LinuxDesktopWindowPresenter*>(user_data)->mapped_ = true;
  return FALSE;
}

void LinuxDesktopWindowPresenter::OnClientMountSizeAllocate(
    GtkWidget* widget, GdkRectangle* allocation, gpointer user_data) {
  (void)widget;
  (void)allocation;
  auto* self = static_cast<LinuxDesktopWindowPresenter*>(user_data);
  self->HandleNativeFrameChanged();
}

gboolean LinuxDesktopWindowPresenter::OnDeleteEvent(GtkWidget* widget,
                                                    GdkEvent* event,
                                                    gpointer user_data) {
  (void)event;
  auto* self = static_cast<LinuxDesktopWindowPresenter*>(user_data);
  if (self->user_close_requested_) {
    return TRUE;
  }
  self->HandleNativeFrameChanged();
  // The close command may remove this window from the model before the
  // publication unloads the presenter; later frame reports would target it.
  self->user_close_requested_ = true;
  self->DisconnectNativeFrameReports();
  auto* host =
      static_cast<LinuxPresentationHost*>(self->presentation_host);
  if (host != nullptr && host->on_desktop_window_user_close != nullptr) {
    host->on_desktop_window_user_close(host->opaque,
                                       self->window.operator->());
  }
  (void)widget;
  return TRUE;
}

void LinuxDesktopWindowPresenter::ApplyRequestedFrameFromMirror() {
  if (applying_mirror_frame_ || window_ == nullptr ||
      !gtk_widget_get_realized(window_)) {
    return;
  }
  auto const desktop = desktop_window();
  if (!desktop) {
    return;
  }
  ClientFrame const mirror = ReadMirrorClientFrame();
  ClientFrame const native = ReadNativeClientFrame();
  if (FramesEqual(mirror, native)) {
    return;
  }
  if (!placement_known_) {
    placement_request_x_ = mirror.x;
    placement_request_y_ = mirror.y;
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
  placement_known_ = false;
  placement_request_x_ = desktop->x();
  placement_request_y_ = desktop->y();

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

  int const client_w = desktop->client_width() > 0 ? desktop->client_width()
                                                     : 640;
  int const client_h = desktop->client_height() > 0 ? desktop->client_height()
                                                      : 480;
  applying_mirror_frame_ = true;
  gtk_window_set_default_size(GTK_WINDOW(window_), client_w, client_h);
  gtk_window_resize(GTK_WINDOW(window_), client_w, client_h);
  gtk_window_move(GTK_WINDOW(window_), desktop->x(), desktop->y());
  // The model frame is a user position. A window mapped at (0,0) without
  // USPosition/PPosition may be treated as unpositioned by an X11 WM.
  gtk_window_set_geometry_hints(GTK_WINDOW(window_), nullptr, nullptr,
                                GDK_HINT_USER_POS);
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
  if (GTK_IS_WIDGET(window_)) {
    gtk_widget_destroy(window_);
  }
  window_ = nullptr;
  client_mount_ = nullptr;
}

}  // namespace apptraverse::ui::linux
