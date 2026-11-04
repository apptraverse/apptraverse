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

gboolean LinuxDesktopWindowPresenter::OnDeleteEvent(GtkWidget* widget,
                                                    GdkEvent* event,
                                                    gpointer user_data) {
  (void)event;
  auto* self = static_cast<LinuxDesktopWindowPresenter*>(user_data);
  auto* host =
      static_cast<LinuxPresentationHost*>(self->presentation_host);
  if (host != nullptr && host->on_desktop_window_user_close != nullptr) {
    host->on_desktop_window_user_close(host->opaque,
                                       self->window.operator->());
  }
  (void)widget;
  return TRUE;
}

void LinuxDesktopWindowPresenter::OnLoad() {
  auto const desktop = desktop_window();

  window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  if (window_ == nullptr) {
    FatalLinux("gtk_window_new");
  }
  g_object_set_data(G_OBJECT(window_), "apptraverse_presenter", this);
  delete_handler_ = g_signal_connect(window_, "delete-event",
                                     G_CALLBACK(OnDeleteEvent), this);

  client_mount_ = gtk_fixed_new();
  if (client_mount_ == nullptr) {
    FatalLinux("gtk_fixed_new");
  }
  gtk_container_add(GTK_CONTAINER(window_), client_mount_);

  int const client_w = desktop->client_width() > 0 ? desktop->client_width()
                                                     : 640;
  int const client_h = desktop->client_height() > 0 ? desktop->client_height()
                                                      : 480;
  gtk_window_set_default_size(GTK_WINDOW(window_), client_w, client_h);
  if (desktop->x() != 0 || desktop->y() != 0) {
    gtk_window_move(GTK_WINDOW(window_), desktop->x(), desktop->y());
  }

  SyncTitleFromMirror();
  gtk_widget_show_all(window_);
}

void LinuxDesktopWindowPresenter::SyncTitleFromMirror() {
  std::string const title_text = ResolveTitleText();
  gtk_window_set_title(GTK_WINDOW(window_), title_text.c_str());
}

void LinuxDesktopWindowPresenter::OnModelChanged() { SyncTitleFromMirror(); }

void LinuxDesktopWindowPresenter::OnUnload() {
  g_signal_handler_disconnect(window_, delete_handler_);
  delete_handler_ = 0;
  if (GTK_IS_WIDGET(window_)) {
    gtk_widget_destroy(window_);
  }
  window_ = nullptr;
  client_mount_ = nullptr;
}

}  // namespace apptraverse::ui::linux
