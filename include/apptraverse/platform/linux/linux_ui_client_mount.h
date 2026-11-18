#ifndef APPTRAVERSE_PLATFORM_LINUX_LINUX_UI_CLIENT_MOUNT_H_
#define APPTRAVERSE_PLATFORM_LINUX_LINUX_UI_CLIENT_MOUNT_H_

#include <gtk/gtk.h>

namespace apptraverse::ui::linux {

// Model-positioned children do not impose a minimum size on the native window.
GtkWidget* CreateClientMount();
void SetNativeSizeRequestIfChanged(GtkWidget* widget, int width, int height);
// gtk_fixed_move queues a resize even when its coordinates are unchanged.
// Compare the native requested geometry, including pending GTK allocation.
void SetClientMountChildFrameIfChanged(GtkWidget* child, int x, int y,
                                       int width, int height);

}  // namespace apptraverse::ui::linux

#endif  // APPTRAVERSE_PLATFORM_LINUX_LINUX_UI_CLIENT_MOUNT_H_
