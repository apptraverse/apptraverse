#include "apptraverse/platform/linux/linux_ui_push_button_presenter.h"

#include "apptraverse/platform/linux/linux_fatal.h"
#include "apptraverse/platform/linux/linux_ui_window_presenter.h"
#include "apptraverse/ui/ui_localization_string.h"

namespace apptraverse::ui::linux {

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
  if (button_ != nullptr) {
    GtkWidget* parent = gtk_widget_get_parent(button_);
    if (parent != nullptr) {
      gtk_container_remove(GTK_CONTAINER(parent), button_);
    }
    gtk_widget_destroy(button_);
    button_ = nullptr;
  }
}

}  // namespace apptraverse::ui::linux
