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
  gtk_widget_set_size_request(list_box_, layout.width, layout.height);
  gtk_fixed_put(GTK_FIXED(parent), list_box_, layout.x, layout.y);
  gtk_widget_show(list_box_);
  SyncFromMirror();
}

void LinuxListContainerPresenter::SyncFromMirror() {
  if (list_box_ == nullptr || !container) {
    return;
  }
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
    gtk_container_add(GTK_CONTAINER(row), text);
    gtk_list_box_insert(box, row, -1);
    gtk_widget_show_all(row);
  }
}

void LinuxListContainerPresenter::OnModelChanged() { SyncFromMirror(); }

void LinuxListContainerPresenter::OnUnload() {
  if (list_box_ != nullptr) {
    GtkWidget* parent = gtk_widget_get_parent(list_box_);
    if (parent != nullptr) {
      gtk_container_remove(GTK_CONTAINER(parent), list_box_);
    }
    gtk_widget_destroy(list_box_);
    list_box_ = nullptr;
  }
}

}  // namespace apptraverse::ui::linux
