#include <cstdlib>
#include <iostream>

#include "apptraverse/platform/linux/linux_ui_client_mount.h"

namespace {

void Check(bool condition, char const* message) {
  if (!condition) {
    std::cerr << "CHECK failed: " << message << '\n';
    std::exit(1);
  }
}

void CheckCanShrink(GtkWidget* mount) {
  GtkRequisition minimum;
  GtkRequisition natural;
  gtk_widget_get_preferred_size(mount, &minimum, &natural);
  Check(minimum.width <= 600 && minimum.height <= 450,
        "previous viewport layout prevents shrinking to a 600x450 window");
  Check(natural.width <= 600 && natural.height <= 450,
        "previous viewport layout expands the native window");
}

}  // namespace

int main(int argc, char** argv) {
  gtk_init(&argc, &argv);
  GtkWidget* mount = apptraverse::ui::linux::CreateClientMount();
  g_object_ref_sink(mount);
  GtkWidget* list = gtk_list_box_new();
  GtkWidget* button = gtk_button_new_with_label("Add");
  gtk_widget_set_size_request(list, 648, 430);
  gtk_widget_set_size_request(button, 100, 32);
  gtk_fixed_put(GTK_FIXED(mount), list, 12, 12);
  gtk_fixed_put(GTK_FIXED(mount), button, 548, 450);
  gtk_widget_show_all(mount);
  CheckCanShrink(mount);

  // A late publication may still contain the previous, larger viewport.
  gtk_widget_set_size_request(list, 900, 700);
  gtk_fixed_move(GTK_FIXED(mount), button, 800, 720);
  CheckCanShrink(mount);

  GtkAllocation smaller{0, 0, 600, 450};
  gtk_widget_size_allocate(mount, &smaller);
  GtkAllocation actual;
  gtk_widget_get_allocation(mount, &actual);
  Check(actual.width == smaller.width && actual.height == smaller.height,
        "client mount accepts the smaller native size");
  gint x = 0;
  gint y = 0;
  gtk_container_child_get(GTK_CONTAINER(mount), button, "x", &x, "y", &y,
                          nullptr);
  Check(x == 800 && y == 720, "client mount preserves model child positions");
  gtk_widget_destroy(mount);
  g_object_unref(mount);
  std::cout << "linux_ui_client_mount_test OK\n";
}
