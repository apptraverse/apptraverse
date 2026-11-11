#include "apptraverse/platform/linux/linux_ui_push_button_presenter.h"

#include "apptraverse/platform/linux/linux_fatal.h"
#include "apptraverse/ui/ui_localization_string.h"

namespace apptraverse::ui::linux {

void LinuxPushButtonPresenter::OnClicked(GtkButton* button, gpointer user_data) {
  (void)button;
  static_cast<LinuxPushButtonPresenter*>(user_data)->NotifyClickedFromUi();
}

void LinuxPushButtonPresenter::OnLoad() {
  auto* parent = static_cast<GtkWidget*>(
      window_presenter->NativeChildParentHandle());

  ui::UiControlLayout const layout =
      window_presenter->ResolveChildLayout({layout_x, layout_y, layout_width,
                                            layout_height, control_id});

  button_ = gtk_button_new_with_label("");
  if (button_ == nullptr) {
    FatalLinux("gtk_button_new_with_label");
  }
  gtk_widget_set_size_request(button_, layout.width, layout.height);
  gtk_fixed_put(GTK_FIXED(parent), button_, layout.x, layout.y);
  clicked_handler_ = g_signal_connect(button_, "clicked",
                                    G_CALLBACK(OnClicked), this);
  gtk_widget_show(button_);
  SyncFromMirror();
}

void LinuxPushButtonPresenter::SyncFromMirror() {
  int const language_index = window_presenter->locale->language_index();
  std::string const label = button->label->Resolve(language_index);
  gtk_button_set_label(GTK_BUTTON(button_), label.c_str());
  gtk_widget_set_sensitive(button_, button->enabled() ? TRUE : FALSE);

  // Layout may change after load (adaptive catalog chrome).
  ui::UiControlLayout const layout =
      window_presenter->ResolveChildLayout({layout_x, layout_y, layout_width,
                                            layout_height, control_id});
  gtk_widget_set_size_request(button_, layout.width, layout.height);
  auto* const parent = static_cast<GtkWidget*>(
      window_presenter->NativeChildParentHandle());
  gtk_fixed_move(GTK_FIXED(parent), button_, layout.x, layout.y);
}

void LinuxPushButtonPresenter::OnModelChanged() { SyncFromMirror(); }

void LinuxPushButtonPresenter::OnUnload() {
  g_signal_handler_disconnect(button_, clicked_handler_);
  clicked_handler_ = 0;
  gtk_widget_destroy(button_);
  button_ = nullptr;
}

}  // namespace apptraverse::ui::linux
