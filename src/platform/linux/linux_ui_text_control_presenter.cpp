#include "apptraverse/platform/linux/linux_ui_text_control_presenter.h"
#include <algorithm>
#include "apptraverse/platform/linux/linux_ui_window_presenter.h"
#include "apptraverse/platform/linux/linux_ui_client_mount.h"
namespace apptraverse::ui::linux {
void LinuxTextControlPresenter::OnChanged(GtkWidget*, gpointer p) {
  static_cast<LinuxTextControlPresenter*>(p)->Edited();
}
void LinuxTextControlPresenter::OnBufferChanged(GtkTextBuffer*, gpointer p) {
  static_cast<LinuxTextControlPresenter*>(p)->Edited();
}
void LinuxTextControlPresenter::OnAccept(GtkEntry*, gpointer p) {
  static_cast<LinuxTextControlPresenter*>(p)->QueueAcceptFromUi();
}
void LinuxTextControlPresenter::Edited() {
  if (applying_) return;
  if (GTK_IS_ENTRY(input_))
    QueueEditedFromUi(gtk_entry_get_text(GTK_ENTRY(input_)));
  else {
    auto* b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(input_));
    GtkTextIter a, z;
    gtk_text_buffer_get_bounds(b, &a, &z);
    auto* t = gtk_text_buffer_get_text(b, &a, &z, FALSE);
    QueueEditedFromUi(t);
    g_free(t);
  }
}
void LinuxTextControlPresenter::OnLoad() {
  if (control->kind == UiTextControlKind::kLabel) {
    widget_ = input_ = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(widget_), 0);
    gtk_label_set_line_wrap(GTK_LABEL(widget_), TRUE);
    gtk_label_set_line_wrap_mode(GTK_LABEL(widget_), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_selectable(GTK_LABEL(widget_), TRUE);
  } else if (control->kind == UiTextControlKind::kEdit) {
    widget_ = input_ = gtk_entry_new();
    g_signal_connect(input_, "changed", G_CALLBACK(OnChanged), this);
    g_signal_connect(input_, "activate", G_CALLBACK(OnAccept), this);
  } else {
    widget_ = gtk_scrolled_window_new(nullptr, nullptr);
    input_ = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(input_), GTK_WRAP_WORD_CHAR);
    gtk_container_add(GTK_CONTAINER(widget_), input_);
    gtk_widget_show(input_);
    g_signal_connect(gtk_text_view_get_buffer(GTK_TEXT_VIEW(input_)), "changed",
                     G_CALLBACK(OnBufferChanged), this);
  }
  gtk_widget_set_name(
      widget_, ("apptraverse-control-" + std::to_string(control_id)).c_str());
  gtk_fixed_put(GTK_FIXED(window_presenter->NativeChildParentHandle()), widget_,
                0, 0);
  Sync();
  static_cast<LinuxDesktopWindowPresenter*>(window_presenter.operator->())
      ->RegisterChildControl(widget_, control_id);
}
void LinuxTextControlPresenter::Sync() {
  applying_ = true;
  atk_object_set_name(gtk_widget_get_accessible(input_),
                      control->accessible_label.empty()
                          ? (control->kind == UiTextControlKind::kLabel
                                 ? control->text.c_str()
                                 : "")
                          : control->accessible_label.c_str());
  std::string text = control->text;
  for (auto const& row : control->rows) {
    if (!text.empty()) text += '\n';
    text += row;
  }
  if (control->revision >= pending_edit) {
    if (GTK_IS_ENTRY(input_)) {
      if (text != gtk_entry_get_text(GTK_ENTRY(input_))) {
        gint a{}, z{};
        gtk_editable_get_selection_bounds(GTK_EDITABLE(input_), &a, &z);
        auto pos = gtk_editable_get_position(GTK_EDITABLE(input_));
        gtk_entry_set_text(GTK_ENTRY(input_), text.c_str());
        gtk_editable_set_position(GTK_EDITABLE(input_), pos);
        if (a != z) gtk_editable_select_region(GTK_EDITABLE(input_), a, z);
      }
    } else if (GTK_IS_TEXT_VIEW(input_)) {
      auto* b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(input_));
      GtkTextIter a, z;
      gtk_text_buffer_get_bounds(b, &a, &z);
      auto* old = gtk_text_buffer_get_text(b, &a, &z, FALSE);
      if (text != old) {
        GtkTextIter cursor, anchor;
        gtk_text_buffer_get_iter_at_mark(b, &cursor,
                                         gtk_text_buffer_get_insert(b));
        gtk_text_buffer_get_iter_at_mark(
            b, &anchor, gtk_text_buffer_get_selection_bound(b));
        auto pos = gtk_text_iter_get_offset(&cursor),
             bound = gtk_text_iter_get_offset(&anchor);
        gtk_text_buffer_set_text(b, text.c_str(), -1);
        gtk_text_buffer_get_iter_at_offset(b, &cursor, pos);
        gtk_text_buffer_get_iter_at_offset(b, &anchor, bound);
        gtk_text_buffer_select_range(b, &cursor, &anchor);
      }
      g_free(old);
    } else if (text != gtk_label_get_text(GTK_LABEL(input_)))
      gtk_label_set_text(GTK_LABEL(input_), text.c_str());
  }
  if (GTK_IS_ENTRY(input_))
    gtk_editable_set_editable(GTK_EDITABLE(input_), !control->read_only);
  if (GTK_IS_TEXT_VIEW(input_))
    gtk_text_view_set_editable(GTK_TEXT_VIEW(input_), !control->read_only);
  gtk_widget_set_sensitive(widget_, control->enabled);
  gtk_widget_set_visible(widget_, control->visible);
  if (!control->visible) {
    applying_ = false;
    return;
  }
  auto r = window_presenter->ResolveChildLayout({0, 0, 1, 1, control_id});
  SetClientMountChildFrameIfChanged(widget_, r.x, r.y, r.width, r.height);
  applying_ = false;
}
void LinuxTextControlPresenter::OnModelChanged() {
  Sync();
  static_cast<LinuxDesktopWindowPresenter*>(window_presenter.operator->())
      ->RefreshLayout();
}
void LinuxTextControlPresenter::OnUnload() {
  static_cast<LinuxDesktopWindowPresenter*>(window_presenter.operator->())
      ->UnregisterChildControl(widget_);
  if (GTK_IS_TEXT_VIEW(input_))
    g_signal_handlers_disconnect_by_data(
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(input_)), this);
  g_signal_handlers_disconnect_by_data(input_, this);
  gtk_widget_destroy(widget_);
  widget_ = input_ = nullptr;
}
}  // namespace apptraverse::ui::linux
