#include "apptraverse/platform/linux/linux_ui_client_mount.h"

namespace {

struct AppTraverseClientMount {
  GtkFixed parent;
};

struct AppTraverseClientMountClass {
  GtkFixedClass parent;
};

G_DEFINE_TYPE(AppTraverseClientMount, apptraverse_client_mount, GTK_TYPE_FIXED)

void PreferredSize(GtkWidget*, gint* minimum, gint* natural) {
  if (minimum != nullptr) *minimum = 0;
  if (natural != nullptr) *natural = 0;
}

void PreferredSizeForConstraint(GtkWidget* widget, gint, gint* minimum,
                                gint* natural) {
  PreferredSize(widget, minimum, natural);
}

void apptraverse_client_mount_class_init(AppTraverseClientMountClass* type) {
  auto* widget = GTK_WIDGET_CLASS(type);
  // Child positions and sizes come from the current model viewport. Propagating
  // them upward as GtkFixed's minimum size prevents a native resize from making
  // that viewport smaller before the next model publication updates the layout.
  widget->get_preferred_width = PreferredSize;
  widget->get_preferred_height = PreferredSize;
  widget->get_preferred_width_for_height = PreferredSizeForConstraint;
  widget->get_preferred_height_for_width = PreferredSizeForConstraint;
}

void apptraverse_client_mount_init(AppTraverseClientMount*) {}

}  // namespace

namespace apptraverse::ui::linux {

GtkWidget* CreateClientMount() {
  return GTK_WIDGET(g_object_new(apptraverse_client_mount_get_type(), nullptr));
}

void SetNativeSizeRequestIfChanged(GtkWidget* widget, int width, int height) {
  int current_width{}, current_height{};
  gtk_widget_get_size_request(widget, &current_width, &current_height);
  if (current_width != width || current_height != height)
    gtk_widget_set_size_request(widget, width, height);
}

void SetClientMountChildFrameIfChanged(GtkWidget* child, int x, int y,
                                       int width, int height) {
  SetNativeSizeRequestIfChanged(child, width, height);
  auto* parent = GTK_CONTAINER(gtk_widget_get_parent(child));
  int current_x{}, current_y{};
  gtk_container_child_get(parent, child, "x", &current_x, "y", &current_y, nullptr);
  if (current_x != x || current_y != y)
    gtk_fixed_move(GTK_FIXED(parent), child, x, y);
}

}  // namespace apptraverse::ui::linux
