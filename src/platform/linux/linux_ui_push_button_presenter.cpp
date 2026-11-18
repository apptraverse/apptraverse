#include "apptraverse/platform/linux/linux_ui_push_button_presenter.h"

#include "apptraverse/platform/linux/linux_fatal.h"
#include "apptraverse/platform/linux/linux_ui_window_presenter.h"
#include "apptraverse/platform/linux/linux_ui_client_mount.h"
#include "apptraverse/ui/ui_localization_string.h"

namespace apptraverse::ui::linux {

void LinuxPushButtonPresenter::OnClicked(GtkButton* button, gpointer user_data) {
  (void)button;
  auto& p = *static_cast<LinuxPushButtonPresenter*>(user_data);
  p.NotifyClickedFromUi();
  auto id = p.button->FocusControlAfterClick();
  if (!id) return;
  auto* parent = GTK_CONTAINER(p.window_presenter->NativeChildParentHandle());
  auto* children = gtk_container_get_children(parent);
  auto const name = "apptraverse-control-" + std::to_string(id);
  for (auto* item = children; item; item = item->next)
    if (name == gtk_widget_get_name(GTK_WIDGET(item->data)))
      gtk_widget_grab_focus(GTK_WIDGET(item->data));
  g_list_free(children);
}

void LinuxPushButtonPresenter::OnLoad() {
  auto* parent =
      static_cast<GtkWidget*>(window_presenter->NativeChildParentHandle());

  ui::UiControlLayout const layout =
      !button->visible()
          ? ui::UiControlLayout{0, 0, 1, 1, control_id}
          : window_presenter->ResolveChildLayout(
                {layout_x, layout_y, layout_width, layout_height, control_id});

  button_ = gtk_button_new_with_label("");
  if (button_ == nullptr) {
    FatalLinux("gtk_button_new_with_label");
  }
  gtk_widget_set_size_request(button_, layout.width, layout.height);
  gtk_fixed_put(GTK_FIXED(parent), button_, layout.x, layout.y);
  clicked_handler_ =
      g_signal_connect(button_, "clicked", G_CALLBACK(OnClicked), this);
  gtk_widget_show(button_);
  SyncFromMirror();
  static_cast<LinuxDesktopWindowPresenter*>(window_presenter.operator->())->RegisterChildControl(button_, control_id);
}

void LinuxPushButtonPresenter::SyncFromMirror() {
  int const language_index = window_presenter->locale->language_index();
  std::string const label = button->label->Resolve(language_index);
  char const* current = gtk_button_get_label(GTK_BUTTON(button_));
  if (!current || label != current) gtk_button_set_label(GTK_BUTTON(button_), label.c_str());
  auto* text = gtk_bin_get_child(GTK_BIN(button_));
  gtk_label_set_line_wrap(GTK_LABEL(text), TRUE);
  gtk_label_set_line_wrap_mode(GTK_LABEL(text), PANGO_WRAP_WORD);
  gtk_widget_set_sensitive(button_, button->enabled() ? TRUE : FALSE);
  gtk_widget_set_visible(button_, button->visible());

  if (!button->visible()) return;
  // Layout may change after load (adaptive catalog chrome).
  ui::UiControlLayout const layout = !button->visible()?ui::UiControlLayout{0,0,1,1,control_id}:
      window_presenter->ResolveChildLayout({layout_x, layout_y, layout_width,
                                            layout_height, control_id});
  SetClientMountChildFrameIfChanged(button_, layout.x, layout.y, layout.width, layout.height);
}

void LinuxPushButtonPresenter::OnModelChanged() { SyncFromMirror(); static_cast<LinuxDesktopWindowPresenter*>(window_presenter.operator->())->RefreshLayout(); }

void LinuxPushButtonPresenter::OnUnload() {
  g_signal_handler_disconnect(button_, clicked_handler_);
  clicked_handler_ = 0;
  static_cast<LinuxDesktopWindowPresenter*>(window_presenter.operator->())->UnregisterChildControl(button_);
  gtk_widget_destroy(button_);
  button_ = nullptr;
}

}  // namespace apptraverse::ui::linux
