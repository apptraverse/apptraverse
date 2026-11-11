#include "apptraverse/platform/linux/linux_ui_list_presenter.h"

#include <algorithm>

#include "apptraverse/platform/linux/linux_fatal.h"
#include "apptraverse/ui/ui_list_container.h"
#include "apptraverse/ui/ui_localization_string.h"

namespace apptraverse::ui::linux {

void LinuxListContainerPresenter::OnRowSelected(GtkListBox* box,
                                              GtkListBoxRow* row,
                                              gpointer user_data) {
  auto* self = static_cast<LinuxListContainerPresenter*>(user_data);
  (void)box;
  self->HandleRowSelected(row);
}

void LinuxListContainerPresenter::HandleRowSelected(GtkListBoxRow* row) {
  if (applying_mirror_sync_ || !notify_list_selection_change || row == nullptr) {
    return;
  }
  int const index = gtk_list_box_row_get_index(row);
  if (index < 0) {
    return;
  }
  SubmitListRowSelectionChangedFromUi(static_cast<std::size_t>(index));
}

void LinuxListContainerPresenter::OnRowActivated(GtkListBox* box,
                                                GtkListBoxRow* row,
                                                gpointer user_data) {
  (void)box;
  auto* self = static_cast<LinuxListContainerPresenter*>(user_data);
  if (self->applying_mirror_sync_ || row == nullptr) {
    return;
  }
  int const index = gtk_list_box_row_get_index(row);
  if (index >= 0) {
    self->SubmitListRowActivatedFromUi(static_cast<std::size_t>(index));
  }
}

void LinuxListContainerPresenter::OnLoad() {
  auto* parent = static_cast<GtkWidget*>(
      window_presenter->NativeChildParentHandle());

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
  row_activated_handler_ = g_signal_connect(
      list_box_, "row-activated", G_CALLBACK(OnRowActivated), this);
  gtk_fixed_put(GTK_FIXED(parent), list_box_, layout.x, layout.y);
  gtk_widget_show(list_box_);
  SyncFromMirror();
}

LinuxListContainerPresenter::NativeRow
LinuxListContainerPresenter::CreateNativeRow(ae::ObjId row_id,
                                             std::string const& label) {
  NativeRow native{};
  native.row_id = row_id;
  native.row = gtk_list_box_row_new();
  native.label = gtk_label_new(label.c_str());
  if (native.row == nullptr || native.label == nullptr) {
    FatalLinux("create GTK list row");
  }
  native.text = label;
  gtk_widget_set_halign(native.label, GTK_ALIGN_START);
  gtk_widget_set_tooltip_text(native.row, label.c_str());
  gtk_container_add(GTK_CONTAINER(native.row), native.label);
  gtk_widget_show_all(native.row);
  return native;
}

void LinuxListContainerPresenter::SyncFromMirror() {
  applying_mirror_sync_ = true;
  int const language_index = window_presenter->locale->language_index();
  GtkListBox* const box = GTK_LIST_BOX(list_box_);

  // Layout may change after load (adaptive catalog chrome).
  ui::UiControlLayout const layout =
      window_presenter->ResolveChildLayout({layout_x, layout_y, layout_width,
                                            layout_height, control_id});
  gtk_widget_set_size_request(list_box_, layout.width, layout.height);
  auto* const parent = static_cast<GtkWidget*>(
      window_presenter->NativeChildParentHandle());
  gtk_fixed_move(GTK_FIXED(parent), list_box_, layout.x, layout.y);

  std::vector<NativeRow> previous = std::move(native_rows_);
  native_rows_.clear();
  native_rows_.reserve(container->rows.size());
  for (UiListRow::ptr const& row : container->rows) {
    ae::ObjId const row_id = row.id();
    std::string const label =
        ResolveListRowPrimaryText(row, language_index);
    auto existing = std::find_if(
        previous.begin(), previous.end(),
        [&](NativeRow const& native) { return native.row_id == row_id; });
    if (existing == previous.end()) {
      native_rows_.push_back(CreateNativeRow(row_id, label));
      continue;
    }
    NativeRow native = *existing;
    previous.erase(existing);
    if (native.text != label) {
      gtk_label_set_text(GTK_LABEL(native.label), label.c_str());
      gtk_widget_set_tooltip_text(native.row, label.c_str());
      native.text = label;
    }
    native_rows_.push_back(native);
  }
  for (NativeRow const& removed : previous) {
    gtk_widget_destroy(removed.row);
  }

  for (std::size_t i = 0; i < native_rows_.size(); ++i) {
    GtkWidget* const row = native_rows_[i].row;
    int const index = static_cast<int>(i);
    if (gtk_widget_get_parent(row) == nullptr) {
      gtk_list_box_insert(box, row, index);
      continue;
    }
    if (gtk_list_box_row_get_index(GTK_LIST_BOX_ROW(row)) == index) {
      continue;
    }
    g_object_ref(row);
    gtk_container_remove(GTK_CONTAINER(box), row);
    gtk_list_box_insert(box, row, index);
    g_object_unref(row);
  }

  if (notify_list_selection_change) {
    std::int32_t const desired = container->selection_index;
    GtkListBoxRow* const current = gtk_list_box_get_selected_row(box);
    GtkListBoxRow* const target =
        desired == -1
            ? nullptr
            : GTK_LIST_BOX_ROW(native_rows_[static_cast<std::size_t>(desired)].row);
    if (target != current) {
      if (target != nullptr) {
        gtk_list_box_select_row(box, target);
      } else {
        gtk_list_box_unselect_all(box);
      }
    }
  }

  applying_mirror_sync_ = false;
}

void LinuxListContainerPresenter::OnModelChanged() { SyncFromMirror(); }

void LinuxListContainerPresenter::OnUnload() {
  g_signal_handler_disconnect(list_box_, row_selected_handler_);
  g_signal_handler_disconnect(list_box_, row_activated_handler_);
  row_selected_handler_ = 0;
  row_activated_handler_ = 0;
  gtk_widget_destroy(list_box_);
  list_box_ = nullptr;
  native_rows_.clear();
}

}  // namespace apptraverse::ui::linux
