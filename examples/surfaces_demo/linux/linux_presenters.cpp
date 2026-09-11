#include "linux_presenters.h"

#include <cstdio>

#include "linux_app.h"
#include "linux_fatal.h"
#include "surfaces_linux_messages.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(LinuxSurfacePresenter);

void OnAddClicked(GtkButton*, gpointer user_data) {
  auto* presenter = static_cast<LinuxSurfacePresenter*>(user_data);
  presenter->OnCommand(kSurfaceAddButtonId, kLinuxButtonClicked);
}

void OnCloseClicked(GtkButton*, gpointer user_data) {
  auto* presenter = static_cast<LinuxSurfacePresenter*>(user_data);
  presenter->OnCommand(kSurfaceCloseButtonId, kLinuxButtonClicked);
}

gboolean OnDeleteEvent(GtkWidget*, GdkEvent*, gpointer user_data) {
  auto* presenter = static_cast<LinuxSurfacePresenter*>(user_data);
  // Native window close → whole-application stop. Never RemoveSurface.
  presenter->app->PostApplicationStop();
  return TRUE;
}

gboolean OnFocusIn(GtkWidget*, GdkEventFocus*, gpointer user_data) {
  auto* presenter = static_cast<LinuxSurfacePresenter*>(user_data);
  presenter->PageShown();
  return FALSE;
}

}  // namespace

void EnsureLinuxSurfacePresenterRegistration() {
  EnsureDesktopSurfacePresenterRegistration();
  (void)&g_apptraverse_registrar_LinuxSurfacePresenter;
}

void LinuxSurfacePresenter::OnLoad() {
  app = static_cast<LinuxApp*>(presentation_host);

  window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  char title[64];
  std::snprintf(title, sizeof(title), "Surface %u", surface->number);
  gtk_window_set_title(GTK_WINDOW(window), title);

  int const w = surface->desktop_width > 0 ? surface->desktop_width : 360;
  int const h = surface->desktop_height > 0 ? surface->desktop_height : 240;
  gtk_window_set_default_size(GTK_WINDOW(window), w, h);
  app->PlaceOuterWindow(window, surface->desktop_x, surface->desktop_y, w, h);

  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_container_set_border_width(GTK_CONTAINER(box), 12);
  gtk_container_add(GTK_CONTAINER(window), box);

  add_button = gtk_button_new_with_label("Add");
  close_button = gtk_button_new_with_label("Close this window");
  gtk_box_pack_start(GTK_BOX(box), add_button, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(box), close_button, FALSE, FALSE, 0);

  g_signal_connect(add_button, "clicked", G_CALLBACK(OnAddClicked), this);
  g_signal_connect(close_button, "clicked", G_CALLBACK(OnCloseClicked), this);
  g_signal_connect(window, "delete-event", G_CALLBACK(OnDeleteEvent), this);
  g_signal_connect(window, "focus-in-event", G_CALLBACK(OnFocusIn), this);

  app->RegisterPresenter(window, this);
  gtk_widget_show_all(window);
}

void LinuxSurfacePresenter::OnModelChanged() {}

void LinuxSurfacePresenter::OnUnload() {
  app->UnregisterPresenter(window);
  // destroy window (and children) under our control after model lifecycle.
  gtk_widget_destroy(window);
  window = nullptr;
  add_button = nullptr;
  close_button = nullptr;
  app = nullptr;
}

bool LinuxSurfacePresenter::OnCommand(std::uint32_t command_id,
                                      std::uint16_t notification_code) {
  if (notification_code != kLinuxButtonClicked) {
    return false;
  }
  if (command_id == kSurfaceAddButtonId) {
    AddClick();
    return true;
  }
  if (command_id == kSurfaceCloseButtonId) {
    if (surface->surfaces->surfaces.size() == 1) {
      app->PostApplicationStop();
    } else {
      RemoveClick();
    }
    return true;
  }
  return false;
}

void LinuxSurfacePresenter::QueueCurrentBounds() {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  app->ReadOuterBounds(window, &x, &y, &width, &height);
  UpdateModelBounds(x, y, width, height);
}

}  // namespace apptraverse
