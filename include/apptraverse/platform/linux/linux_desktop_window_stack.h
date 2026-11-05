#ifndef APPTRAVERSE_PLATFORM_LINUX_LINUX_DESKTOP_WINDOW_STACK_H_
#define APPTRAVERSE_PLATFORM_LINUX_LINUX_DESKTOP_WINDOW_STACK_H_

#include <vector>

typedef struct _GtkWidget GtkWidget;

namespace apptraverse::ui::linux {

bool LinuxDesktopStackUsesX11();

// Topmost window first. Empty if stacking is unavailable (e.g. pure Wayland).
std::vector<GtkWidget*> CaptureDesktopStackingOrderTopToBottom(
    std::vector<GtkWidget*> const& top_level_windows);

// `top_to_bottom`: topmost GtkWidget first; raises bottom→top without touching
// windows outside the list.
void ApplyDesktopStackingOrderTopToBottom(
    std::vector<GtkWidget*> const& top_to_bottom);

}  // namespace apptraverse::ui::linux

#endif  // APPTRAVERSE_PLATFORM_LINUX_LINUX_DESKTOP_WINDOW_STACK_H_
