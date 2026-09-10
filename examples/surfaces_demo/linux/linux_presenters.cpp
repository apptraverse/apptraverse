#include "linux_presenters.h"

#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <cstdio>
#include <cstring>
#include <unistd.h>

#include "linux_app.h"
#include "linux_fatal.h"
#include "surfaces_linux_messages.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(LinuxSurfacePresenter);

void DrawButton(Display* display, Window window, GC gc, int x, int y, int w,
                int h, char const* label) {
  XDrawRectangle(display, window, gc, x, y, static_cast<unsigned int>(w - 1),
                 static_cast<unsigned int>(h - 1));
  int const text_x = x + 10;
  int const text_y = y + h / 2 + 4;
  XDrawString(display, window, gc, text_x, text_y, label,
              static_cast<int>(std::strlen(label)));
}

bool Hit(int px, int py, int x, int y, int w, int h) {
  return px >= x && py >= y && px < x + w && py < y + h;
}

}  // namespace

void EnsureLinuxSurfacePresenterRegistration() {
  EnsureDesktopSurfacePresenterRegistration();
  (void)&g_apptraverse_registrar_LinuxSurfacePresenter;
}

void LinuxSurfacePresenter::OnLoad() {
  app = static_cast<LinuxApp*>(presentation_host);
  Display* const display = app->display();
  Window const root = DefaultRootWindow(display);
  unsigned long const black = BlackPixel(display, DefaultScreen(display));
  unsigned long const white = WhitePixel(display, DefaultScreen(display));

  // Create with persisted outer size as an initial client size; placement is
  // corrected to outer top-left after map via _NET_FRAME_EXTENTS.
  unsigned int create_w =
      surface->desktop_width > 0
          ? static_cast<unsigned int>(surface->desktop_width)
          : 360u;
  unsigned int create_h =
      surface->desktop_height > 0
          ? static_cast<unsigned int>(surface->desktop_height)
          : 240u;
  window = XCreateSimpleWindow(display, root, surface->desktop_x,
                               surface->desktop_y, create_w, create_h, 1, black,
                               white);
  if (window == None) {
    FatalLinux("XCreateSimpleWindow Surface");
  }

  char title[64];
  std::snprintf(title, sizeof(title), "Surface %u", surface->number);
  XStoreName(display, window, title);
  XClassHint hint{};
  hint.res_name = const_cast<char*>("apptraverse_surfaces");
  hint.res_class = const_cast<char*>("AppTraverseSurfaces");
  XSetClassHint(display, window, &hint);

  Atom net_wm_pid = XInternAtom(display, "_NET_WM_PID", False);
  pid_t const pid = getpid();
  XChangeProperty(display, window, net_wm_pid, XA_CARDINAL, 32, PropModeReplace,
                  reinterpret_cast<unsigned char const*>(&pid), 1);

  Atom wm_delete = app->wm_delete();
  XSetWMProtocols(display, window, &wm_delete, 1);
  XSelectInput(display, window,
               ExposureMask | ButtonPressMask | StructureNotifyMask |
                   KeyPressMask);
  app->RegisterPresenter(window, this);
  XMapWindow(display, window);
  XSync(display, False);

  // After the WM reparents, push outer placement from model fields.
  app->PlaceOuterWindow(window, surface->desktop_x, surface->desktop_y,
                        surface->desktop_width, surface->desktop_height);
}

void LinuxSurfacePresenter::OnModelChanged() {}

void LinuxSurfacePresenter::OnUnload() {
  Display* const display = app->display();
  app->UnregisterPresenter(window);
  XDestroyWindow(display, window);
  XFlush(display);
  window = None;
  app = nullptr;
}

bool LinuxSurfacePresenter::OnCommand(std::uint32_t command_id,
                                      std::uint16_t notification_code) {
  if (notification_code != kLinuxButtonClicked) {
    return false;
  }
  if (command_id == kSurfaceAddButtonId) {
    AddClick();
    return true;
  }
  if (command_id == kSurfaceCloseButtonId) {
    // Real alternative: last Close-button closes the app without Remove.
    if (surface->surfaces->surfaces.size() == 1) {
      app->PostApplicationStop();
    } else {
      RemoveClick();
    }
    return true;
  }
  return false;
}

void LinuxSurfacePresenter::QueueCurrentBounds() {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  app->ReadOuterBounds(window, &x, &y, &width, &height);
  UpdateModelBounds(x, y, width, height);
}

void LinuxSurfacePresenter::HandleEvent(XEvent const& event) {
  Display* const display = app->display();
  if (event.type == Expose && event.xexpose.count == 0) {
    GC gc = XCreateGC(display, window, 0, nullptr);
    XClearWindow(display, window);
    char title[64];
    std::snprintf(title, sizeof(title), "Surface %u", surface->number);
    XDrawString(display, window, gc, 12, 60, title,
                static_cast<int>(std::strlen(title)));
    DrawButton(display, window, gc, add_x, add_y, add_w, add_h, "Add");
    DrawButton(display, window, gc, close_x, close_y, close_w, close_h,
               "Close this window");
    XFreeGC(display, gc);
    return;
  }
  if (event.type == ButtonPress && event.xbutton.button == Button1) {
    int const px = event.xbutton.x;
    int const py = event.xbutton.y;
    if (Hit(px, py, add_x, add_y, add_w, add_h)) {
      OnCommand(kSurfaceAddButtonId, kLinuxButtonClicked);
      return;
    }
    if (Hit(px, py, close_x, close_y, close_w, close_h)) {
      OnCommand(kSurfaceCloseButtonId, kLinuxButtonClicked);
      return;
    }
  }
  if (event.type == ClientMessage) {
    if (event.xclient.message_type == app->wm_protocols() &&
        static_cast<Atom>(event.xclient.data.l[0]) == app->wm_delete()) {
      // Native X always requests whole-application stop. Never RemoveSurface.
      app->PostApplicationStop();
    }
  }
}

}  // namespace apptraverse
