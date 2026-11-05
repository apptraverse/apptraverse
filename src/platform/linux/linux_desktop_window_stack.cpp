#include "apptraverse/platform/linux/linux_desktop_window_stack.h"

#include <unordered_map>

#include <gdk/gdk.h>
#include <gtk/gtk.h>

#if defined(GDK_WINDOWING_X11)
#include <gdk/gdkx.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#endif

namespace apptraverse::ui::linux {
namespace {

#if defined(GDK_WINDOWING_X11)
Display* X11DisplayOrNull() {
  GdkDisplay* const display = gdk_display_get_default();
  if (display == nullptr || !GDK_IS_X11_DISPLAY(display)) {
    return nullptr;
  }
  return gdk_x11_display_get_xdisplay(display);
}

Atom NetClientListStackingAtom(Display* display) {
  return XInternAtom(display, "_NET_CLIENT_LIST_STACKING", False);
}

std::vector<Window> ReadNetClientListStacking(Display* display, Window root) {
  Atom const atom = NetClientListStackingAtom(display);
  if (atom == None) {
    return {};
  }
  Atom actual_type = None;
  int actual_format = 0;
  unsigned long item_count = 0;
  unsigned long bytes_after = 0;
  unsigned char* data = nullptr;
  if (XGetWindowProperty(display, root, atom, 0, 1024, False, XA_WINDOW,
                         &actual_type, &actual_format, &item_count,
                         &bytes_after, &data) != Success) {
    return {};
  }
  std::vector<Window> windows;
  if (data != nullptr && actual_type == XA_WINDOW && actual_format == 32) {
    auto* ids = reinterpret_cast<Window*>(data);
    windows.assign(ids, ids + item_count);
  }
  if (data != nullptr) {
    XFree(data);
  }
  return windows;
}
#endif

}  // namespace

bool LinuxDesktopStackUsesX11() {
#if defined(GDK_WINDOWING_X11)
  return X11DisplayOrNull() != nullptr;
#else
  return false;
#endif
}

std::vector<GtkWidget*> CaptureDesktopStackingOrderTopToBottom(
    std::vector<GtkWidget*> const& top_level_windows) {
  std::vector<GtkWidget*> result;
#if defined(GDK_WINDOWING_X11)
  Display* const display = X11DisplayOrNull();
  if (display == nullptr) {
    return result;
  }
  std::unordered_map<Window, GtkWidget*> xid_to_widget;
  for (GtkWidget* widget : top_level_windows) {
    if (widget == nullptr || !gtk_widget_get_realized(widget)) {
      continue;
    }
    GdkWindow* const gdk_window = gtk_widget_get_window(widget);
    if (gdk_window == nullptr) {
      continue;
    }
    Window const xid = gdk_x11_window_get_xid(gdk_window);
    xid_to_widget[xid] = widget;
  }
  if (xid_to_widget.empty()) {
    return result;
  }
  Window const root = DefaultRootWindow(display);
  std::vector<Window> const stacking_bottom_to_top =
      ReadNetClientListStacking(display, root);
  if (stacking_bottom_to_top.empty()) {
    return result;
  }
  for (auto it = stacking_bottom_to_top.rbegin();
       it != stacking_bottom_to_top.rend(); ++it) {
    auto const found = xid_to_widget.find(*it);
    if (found != xid_to_widget.end()) {
      result.push_back(found->second);
    }
  }
#endif
  return result;
}

void ApplyDesktopStackingOrderTopToBottom(
    std::vector<GtkWidget*> const& top_to_bottom) {
  if (top_to_bottom.empty()) {
    return;
  }
  for (auto it = top_to_bottom.rbegin(); it != top_to_bottom.rend(); ++it) {
    GtkWidget* const widget = *it;
    if (widget == nullptr || !gtk_widget_get_realized(widget)) {
      continue;
    }
    GdkWindow* const gdk_window = gtk_widget_get_window(widget);
    if (gdk_window == nullptr) {
      continue;
    }
    gdk_window_raise(gdk_window);
  }
}

}  // namespace apptraverse::ui::linux
