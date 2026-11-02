#include "apptraverse/platform/linux/linux_ui_list_presenter.h"

#include "apptraverse/platform/linux/linux_fatal.h"
#include "apptraverse/platform/linux/linux_ui_window_presenter.h"
#include "apptraverse/ui/ui_list_container.h"
#include "apptraverse/ui/ui_localization_string.h"

namespace apptraverse::ui::linux {

LinuxDesktopWindowPresenter* LinuxListContainerPresenter::Desktop() const {
  if (!window_presenter) {
    return nullptr;
  }
  return static_cast<LinuxDesktopWindowPresenter*>(window_presenter.operator->());
}

void LinuxListContainerPresenter::OnRowSelected(GtkListBox* box,
                                              GtkListBoxRow* row,
                                              gpointer user_data) {
  auto* self = static_cast<LinuxListContainerPresenter*>(user_data);
  (void)box;
  self->HandleRowSelected(row);
}

void LinuxListContainerPresenter::HandleRowSelected(GtkListBoxRow* row) {
  if (applying_mirror_sync_ || row == nullptr) {
    return;
  }
  int const index = gtk_list_box_row_get_index(row);
  if (index < 0) {
    return;
  }
  SubmitListRowSelectionChangedFromUi(static_cast<std::size_t>(index));
}

void LinuxListContainerPresenter::OnLoad() {
  LinuxDesktopWindowPresenter* desktop = Desktop();
  GtkWidget* parent = desktop != nullptr ? desktop->ClientMount() : nullptr;
  if (parent == nullptr || !container) {
    FatalLinux("LinuxListContainerPresenter::OnLoad missing parent");
  }

  ui::UiControlLayout const layout =
      window_presenter->ResolveChildLayout({layout_x, layout_y, layout_width,
                                            layout_height, control_id});

  list_box_ = gtk_list_box_new();
  if (list_box_ == nullptr) {
    FatalLinux("gtk_list_box_new");
  }
  gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(list_box_), FALSE);
  gtk_widget_set_size_request(list_box_, layout.width, layout.height);
  row_selected_handler_ = g_signal_connect(
      list_box_, "row-selected", G_CALLBACK(OnRowSelected), this);
  gtk_fixed_put(GTK_FIXED(parent), list_box_, layout.x, layout.y);
  gtk_widget_show(list_box_);
  SyncFromMirror();
}

void LinuxListContainerPresenter::SyncFromMirror() {
  if (list_box_ == nullptr || !container) {
    return;
  }
  applying_mirror_sync_ = true;
  container.Load();
  int language_index = 0;
  if (window_presenter && window_presenter->locale) {
    window_presenter->locale.Load();
    language_index = window_presenter->locale->language_index();
  }
  std::vector<std::string> const labels =
      ResolvePresentedListRowPrimaryTexts(*container, language_index);

  GtkListBox* const box = GTK_LIST_BOX(list_box_);
  while (GtkListBoxRow* row = gtk_list_box_get_row_at_index(box, 0)) {
    gtk_widget_destroy(GTK_WIDGET(row));
  }

  for (std::string const& label : labels) {
    GtkWidget* row = gtk_list_box_row_new();
    GtkWidget* text = gtk_label_new(label.c_str());
    gtk_widget_set_halign(text, GTK_ALIGN_START);
    gtk_widget_set_tooltip_text(row, label.c_str());
    gtk_container_add(GTK_CONTAINER(row), text);
    gtk_list_box_insert(box, row, -1);
    gtk_widget_show_all(row);
  }

  if (notify_list_selection_change) {
    std::int32_t const desired = container->selection_index;
    if (desired >= 0 &&
        desired < static_cast<std::int32_t>(labels.size())) {
      GtkListBoxRow* row = gtk_list_box_get_row_at_index(box, desired);
      if (row != nullptr) {
        gtk_list_box_select_row(box, row);
      }
    } else {
      gtk_list_box_unselect_all(box);
    }
  }

  applying_mirror_sync_ = false;
}

void LinuxListContainerPresenter::OnModelChanged() { SyncFromMirror(); }

void LinuxListContainerPresenter::OnUnload() {
  if (list_box_ == nullptr) {
    return;
  }
  if (row_selected_handler_ != 0) {
    g_signal_handler_disconnect(list_box_, row_selected_handler_);
    row_selected_handler_ = 0;
  }
  if (GTK_IS_WIDGET(list_box_)) {
    gtk_widget_destroy(list_box_);
  }
  list_box_ = nullptr;
}

}  // namespace apptraverse::ui::linux
