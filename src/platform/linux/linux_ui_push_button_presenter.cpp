#include "apptraverse/platform/linux/linux_ui_push_button_presenter.h"

#include "apptraverse/platform/linux/linux_fatal.h"
#include "apptraverse/platform/linux/linux_ui_window_presenter.h"
#include "apptraverse/ui/ui_localization_string.h"

namespace apptraverse::ui::linux {

void LinuxPushButtonPresenter::OnClicked(GtkButton* button, gpointer user_data) {
  (void)button;
  static_cast<LinuxPushButtonPresenter*>(user_data)->NotifyClickedFromUi();
}

void LinuxPushButtonPresenter::OnLoad() {
  auto* desktop = static_cast<LinuxDesktopWindowPresenter*>(
      window_presenter.operator->());
  GtkWidget* parent = desktop != nullptr ? desktop->ClientMount() : nullptr;
  if (parent == nullptr) {
    FatalLinux("LinuxPushButtonPresenter::OnLoad missing parent");
  }

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
  if (button_ == nullptr) {
    return;
  }
  button.Load();
  window_presenter.Load();
  window_presenter->locale.Load();
  button->label.Load();
  if (!button->label) {
    FatalLinux("LinuxPushButtonPresenter mirror label missing");
  }
  int const language_index = window_presenter->locale->language_index();
  std::string const label = button->label->Resolve(language_index);
  gtk_button_set_label(GTK_BUTTON(button_), label.c_str());
  gtk_widget_set_sensitive(button_, button->enabled() ? TRUE : FALSE);
}

void LinuxPushButtonPresenter::OnModelChanged() { SyncFromMirror(); }

void LinuxPushButtonPresenter::OnUnload() {
  if (button_ == nullptr) {
    return;
  }
  if (clicked_handler_ != 0) {
    g_signal_handler_disconnect(button_, clicked_handler_);
    clicked_handler_ = 0;
  }
  if (GTK_IS_WIDGET(button_)) {
    gtk_widget_destroy(button_);
  }
  button_ = nullptr;
}

}  // namespace apptraverse::ui::linux
