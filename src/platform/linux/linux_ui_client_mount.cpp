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

}  // namespace apptraverse::ui::linux
