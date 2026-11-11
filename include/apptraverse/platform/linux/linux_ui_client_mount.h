#ifndef APPTRAVERSE_PLATFORM_LINUX_LINUX_UI_CLIENT_MOUNT_H_
#define APPTRAVERSE_PLATFORM_LINUX_LINUX_UI_CLIENT_MOUNT_H_

#include <gtk/gtk.h>

namespace apptraverse::ui::linux {

// Model-positioned children do not impose a minimum size on the native window.
GtkWidget* CreateClientMount();

}  // namespace apptraverse::ui::linux

#endif  // APPTRAVERSE_PLATFORM_LINUX_LINUX_UI_CLIENT_MOUNT_H_
