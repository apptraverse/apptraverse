#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include "aether-objects/obj/registry.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"

#include "linux_app.h"
#include "linux_presenters.h"
#include "surfaces_ids.h"
#include "surfaces_lifecycle.h"
#include "surfaces_linux_messages.h"
#include "surfaces_model.h"

namespace apptraverse::test {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"     \
                << __LINE__ << '\n';                                        \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

int IgnoreBadWindow(Display* display, XErrorEvent* error) {
  // BadMatch on SetInputFocus is common when the window is not yet focused /
  // viewable under the WM; Z-order raise + synthetic FocusIn still apply.
  if (error->error_code == BadWindow || error->error_code == BadDrawable ||
      error->error_code == BadMatch) {
    return 0;
  }
  char buf[256];
  XGetErrorText(display, error->error_code, buf, sizeof(buf));
  std::cerr << "X error: " << buf << " major=" << int{error->request_code}
            << '\n';
  std::exit(1);
  return 0;
}

bool ReadPid(Display* display, Window window, pid_t* out) {
  XWindowAttributes attrs{};
  if (XGetWindowAttributes(display, window, &attrs) == 0) {
    return false;
  }
  Atom net_wm_pid = XInternAtom(display, "_NET_WM_PID", True);
  if (net_wm_pid == None) {
    return false;
  }
  Atom actual_type = None;
  int actual_format = 0;
  unsigned long nitems = 0;
  unsigned long bytes_after = 0;
  unsigned char* prop = nullptr;
  int const rc = XGetWindowProperty(display, window, net_wm_pid, 0, 1, False,
                                    XA_CARDINAL, &actual_type, &actual_format,
                                    &nitems, &bytes_after, &prop);
  if (rc != Success || prop == nullptr || nitems < 1) {
    if (prop != nullptr) {
      XFree(prop);
    }
    return false;
  }
  *out = static_cast<pid_t>(*reinterpret_cast<unsigned long*>(prop));
  XFree(prop);
  return true;
}

bool ReadName(Display* display, Window window, std::string* out) {
  char* name = nullptr;
  if (XFetchName(display, window, &name) == 0 || name == nullptr) {
    return false;
  }
  *out = name;
  XFree(name);
  return true;
}

void CollectOwned(Display* display, Window window, pid_t pid,
                  std::vector<Window>* out) {
  pid_t window_pid = 0;
  if (ReadPid(display, window, &window_pid) && window_pid == pid) {
    out->push_back(window);
  }
  Window root = None;
  Window parent = None;
  Window* children = nullptr;
  unsigned int nchildren = 0;
  if (XQueryTree(display, window, &root, &parent, &children, &nchildren) ==
      0) {
    return;
  }
  for (unsigned int i = 0; i < nchildren; ++i) {
    CollectOwned(display, children[i], pid, out);
  }
  if (children != nullptr) {
    XFree(children);
  }
}

Window FindOwned(Display* display, pid_t pid, char const* title) {
  std::vector<Window> owned;
  CollectOwned(display, DefaultRootWindow(display), pid, &owned);
  for (Window window : owned) {
    std::string name;
    if (!ReadName(display, window, &name)) {
      continue;
    }
    if (name == title) {
      XWindowAttributes attrs{};
      if (XGetWindowAttributes(display, window, &attrs) != 0 &&
          attrs.map_state == IsViewable) {
        return window;
      }
    }
  }
  return None;
}

int CountOwnedSurfaces(Display* display, pid_t pid) {
  std::vector<Window> owned;
  CollectOwned(display, DefaultRootWindow(display), pid, &owned);
  int count = 0;
  for (Window window : owned) {
    std::string name;
    if (!ReadName(display, window, &name)) {
      continue;
    }
    if (name.rfind("Surface ", 0) == 0) {
      XWindowAttributes attrs{};
      if (XGetWindowAttributes(display, window, &attrs) != 0 &&
          attrs.map_state == IsViewable) {
        ++count;
      }
    }
  }
  return count;
}

void Pump(Display* display, std::chrono::milliseconds slice) {
  auto const deadline = std::chrono::steady_clock::now() + slice;
  while (std::chrono::steady_clock::now() < deadline) {
    while (XPending(display) > 0) {
      XEvent event{};
      XNextEvent(display, &event);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
}

bool WaitOwned(Display* display, pid_t pid, char const* title, Window* out,
               std::chrono::milliseconds timeout) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    Window window = FindOwned(display, pid, title);
    if (window != None) {
      if (out != nullptr) {
        *out = window;
      }
      return true;
    }
    Pump(display, std::chrono::milliseconds{20});
  }
  return false;
}

bool WaitCount(Display* display, pid_t pid, int expected,
               std::chrono::milliseconds timeout) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (CountOwnedSurfaces(display, pid) == expected) {
      return true;
    }
    Pump(display, std::chrono::milliseconds{20});
  }
  return false;
}

void ClickButton(Display* display, Window window, int x, int y) {
  XEvent event{};
  event.type = ButtonPress;
  event.xbutton.display = display;
  event.xbutton.window = window;
  event.xbutton.root = DefaultRootWindow(display);
  event.xbutton.subwindow = None;
  event.xbutton.time = CurrentTime;
  event.xbutton.x = x;
  event.xbutton.y = y;
  event.xbutton.x_root = x;
  event.xbutton.y_root = y;
  event.xbutton.state = Button1Mask;
  event.xbutton.button = Button1;
  event.xbutton.same_screen = True;
  CHECK(XSendEvent(display, window, False, ButtonPressMask, &event) != 0);
  XFlush(display);
}

void ClickAdd(Display* display, Window window) {
  // Matches LinuxSurfacePresenter button rect center.
  ClickButton(display, window, 12 + 40, 12 + 14);
}

void ClickClose(Display* display, Window window) {
  ClickButton(display, window, 100 + 80, 12 + 14);
}

void SendWmDelete(Display* display, Window window) {
  Atom wm_protocols = XInternAtom(display, "WM_PROTOCOLS", False);
  Atom wm_delete = XInternAtom(display, "WM_DELETE_WINDOW", False);
  XEvent event{};
  event.type = ClientMessage;
  event.xclient.display = display;
  event.xclient.window = window;
  event.xclient.message_type = wm_protocols;
  event.xclient.format = 32;
  event.xclient.data.l[0] = static_cast<long>(wm_delete);
  event.xclient.data.l[1] = CurrentTime;
  CHECK(XSendEvent(display, window, False, NoEventMask, &event) != 0);
  XFlush(display);
}

struct Rect {
  int x;
  int y;
  int width;
  int height;
};

bool QueryFrameExtents(Display* display, Window window, long* left, long* right,
                       long* top, long* bottom) {
  Atom net_frame_extents = XInternAtom(display, "_NET_FRAME_EXTENTS", True);
  if (net_frame_extents == None) {
    return false;
  }
  Atom actual_type = None;
  int actual_format = 0;
  unsigned long nitems = 0;
  unsigned long bytes_after = 0;
  unsigned char* prop = nullptr;
  int const rc = XGetWindowProperty(
      display, window, net_frame_extents, 0, 4, False, XA_CARDINAL,
      &actual_type, &actual_format, &nitems, &bytes_after, &prop);
  if (rc != Success || prop == nullptr || nitems < 4 || actual_format != 32) {
    if (prop != nullptr) {
      XFree(prop);
    }
    return false;
  }
  long const* values = reinterpret_cast<long const*>(prop);
  *left = values[0];
  *right = values[1];
  *top = values[2];
  *bottom = values[3];
  XFree(prop);
  return true;
}

Rect ReadOuter(Display* display, Window window) {
  Window root_return = None;
  Window child = None;
  int win_x = 0;
  int win_y = 0;
  unsigned int win_w = 0;
  unsigned int win_h = 0;
  unsigned int border = 0;
  unsigned int depth = 0;
  CHECK(XGetGeometry(display, window, &root_return, &win_x, &win_y, &win_w,
                     &win_h, &border, &depth) != 0);
  int root_x = 0;
  int root_y = 0;
  CHECK(XTranslateCoordinates(display, window, DefaultRootWindow(display), 0, 0,
                              &root_x, &root_y, &child) != False);
  long left = 0;
  long right = 0;
  long top = 0;
  long bottom = 0;
  Rect rect{};
  if (QueryFrameExtents(display, window, &left, &right, &top, &bottom)) {
    rect.x = root_x - static_cast<int>(left);
    rect.y = root_y - static_cast<int>(top);
    rect.width = static_cast<int>(win_w) + static_cast<int>(left + right);
    rect.height = static_cast<int>(win_h) + static_cast<int>(top + bottom);
  } else {
    rect.x = root_x;
    rect.y = root_y;
    rect.width = static_cast<int>(win_w);
    rect.height = static_cast<int>(win_h);
  }
  return rect;
}

void PlaceOuter(Display* display, Window window, int outer_x, int outer_y,
                int outer_w, int outer_h) {
  long left = 0;
  long right = 0;
  long top = 0;
  long bottom = 0;
  bool const have_extents =
      QueryFrameExtents(display, window, &left, &right, &top, &bottom);
  int client_w = outer_w;
  int client_h = outer_h;
  int client_x = outer_x;
  int client_y = outer_y;
  if (have_extents) {
    client_w = outer_w - static_cast<int>(left + right);
    client_h = outer_h - static_cast<int>(top + bottom);
    if (client_w < 1) {
      client_w = 1;
    }
    if (client_h < 1) {
      client_h = 1;
    }
    client_x = outer_x + static_cast<int>(left);
    client_y = outer_y + static_cast<int>(top);
  }
  XMoveResizeWindow(display, window, client_x, client_y,
                    static_cast<unsigned int>(client_w),
                    static_cast<unsigned int>(client_h));
  XFlush(display);
}

bool RectNear(Rect const& a, Rect const& b, int tol) {
  return std::abs(a.x - b.x) <= tol && std::abs(a.y - b.y) <= tol &&
         std::abs(a.width - b.width) <= tol &&
         std::abs(a.height - b.height) <= tol;
}

void TestPresenterHierarchy() {
  EnsureObjectRegistration();
  EnsureSurfacesModelRegistration();
  EnsureLinuxSurfacePresenterRegistration();
  auto& registry = ae::Registry::GetRegistry();
  CHECK(registry.GenerationDistance(SurfacePresenter::kClassId,
                                    DesktopSurfacePresenter::kClassId) == 1);
  CHECK(registry.GenerationDistance(DesktopSurfacePresenter::kClassId,
                                    LinuxSurfacePresenter::kClassId) == 1);
  CHECK(registry.GenerationDistance(SurfacePresenter::kClassId,
                                    LinuxSurfacePresenter::kClassId) == 2);
}

void TestCloseButtonRemovesOne() {
  pid_t const pid = getpid();
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_surfaces_linux_close_btn";
  std::filesystem::remove_all(dir);

  EnsureObjectRegistration();
  EnsureSurfacesModelRegistration();
  EnsureLinuxSurfacePresenterRegistration();

  LinuxApp app;
  std::thread gui{[&] { CHECK(app.Run(dir) == 0); }};

  Display* display = XOpenDisplay(nullptr);
  CHECK(display != nullptr);

  Window s1 = None;
  CHECK(WaitOwned(display, pid, "Surface 1", &s1, std::chrono::seconds{30}));
  ClickAdd(display, s1);
  Window s2 = None;
  CHECK(WaitOwned(display, pid, "Surface 2", &s2, std::chrono::seconds{30}));
  ClickAdd(display, s2);
  Window s3 = None;
  CHECK(WaitOwned(display, pid, "Surface 3", &s3, std::chrono::seconds{30}));
  CHECK(WaitCount(display, pid, 3, std::chrono::seconds{10}));

  ClickClose(display, s2);
  CHECK(WaitCount(display, pid, 2, std::chrono::seconds{30}));
  CHECK(FindOwned(display, pid, "Surface 1") == s1);
  CHECK(FindOwned(display, pid, "Surface 3") == s3);
  CHECK(FindOwned(display, pid, "Surface 2") == None);

  ClickAdd(display, s3);
  Window s4 = None;
  CHECK(WaitOwned(display, pid, "Surface 4", &s4, std::chrono::seconds{30}));
  CHECK(WaitCount(display, pid, 3, std::chrono::seconds{10}));
  CHECK(FindOwned(display, pid, "Surface 2") == None);

  PlaceOuter(display, s1, 60, 70, 380, 250);
  PlaceOuter(display, s3, 160, 170, 400, 260);
  PlaceOuter(display, s4, 260, 270, 420, 270);
  Pump(display, std::chrono::milliseconds{100});
  Rect const r1 = ReadOuter(display, s1);
  Rect const r3 = ReadOuter(display, s3);
  Rect const r4 = ReadOuter(display, s4);

  SendWmDelete(display, s3);
  gui.join();
  CHECK(CountOwnedSurfaces(display, pid) == 0);

  DirectoryDomainStorage storage{dir};
  ae::Domain domain{storage};
  auto application = LoadApplication<Application>(
      domain, ae::ObjId{surfaces_demo::ToObjId(
                  surfaces_demo::ObjId::Application)});
  CHECK(application->surfaces->surfaces.size() == 3);
  CHECK(application->surfaces->surfaces[0]->number == 1);
  CHECK(application->surfaces->surfaces[1]->number == 3);
  CHECK(application->surfaces->surfaces[2]->number == 4);

  LinuxApp app2;
  std::thread gui2{[&] { CHECK(app2.Run(dir) == 0); }};
  Window rs1 = None;
  Window rs3 = None;
  Window rs4 = None;
  CHECK(WaitOwned(display, pid, "Surface 1", &rs1, std::chrono::seconds{30}));
  CHECK(WaitOwned(display, pid, "Surface 3", &rs3, std::chrono::seconds{30}));
  CHECK(WaitOwned(display, pid, "Surface 4", &rs4, std::chrono::seconds{30}));
  CHECK(CountOwnedSurfaces(display, pid) == 3);
  CHECK(FindOwned(display, pid, "Surface 2") == None);
  // X11 frame extents / WM placement can shift by decoration size.
  constexpr int kTol = 40;
  CHECK(RectNear(ReadOuter(display, rs1), r1, kTol));
  CHECK(RectNear(ReadOuter(display, rs3), r3, kTol));
  CHECK(RectNear(ReadOuter(display, rs4), r4, kTol));

  SendWmDelete(display, rs1);
  gui2.join();
  XCloseDisplay(display);
  std::filesystem::remove_all(dir);
}

void TestNativeXKeepsAllSurfaces() {
  pid_t const pid = getpid();
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_surfaces_linux_native_x";
  std::filesystem::remove_all(dir);

  EnsureObjectRegistration();
  EnsureSurfacesModelRegistration();
  EnsureLinuxSurfacePresenterRegistration();

  LinuxApp app;
  std::thread gui{[&] { CHECK(app.Run(dir) == 0); }};

  Display* display = XOpenDisplay(nullptr);
  CHECK(display != nullptr);

  Window s1 = None;
  CHECK(WaitOwned(display, pid, "Surface 1", &s1, std::chrono::seconds{30}));
  ClickAdd(display, s1);
  Window s2 = None;
  CHECK(WaitOwned(display, pid, "Surface 2", &s2, std::chrono::seconds{30}));
  ClickAdd(display, s2);
  Window s3 = None;
  CHECK(WaitOwned(display, pid, "Surface 3", &s3, std::chrono::seconds{30}));
  CHECK(WaitCount(display, pid, 3, std::chrono::seconds{10}));

  PlaceOuter(display, s1, 50, 60, 370, 240);
  PlaceOuter(display, s2, 150, 160, 390, 250);
  PlaceOuter(display, s3, 250, 260, 410, 260);
  Pump(display, std::chrono::milliseconds{100});
  Rect const r1 = ReadOuter(display, s1);
  Rect const r2 = ReadOuter(display, s2);
  Rect const r3 = ReadOuter(display, s3);

  SendWmDelete(display, s2);
  gui.join();
  CHECK(CountOwnedSurfaces(display, pid) == 0);

  DirectoryDomainStorage storage{dir};
  ae::Domain domain{storage};
  auto application = LoadApplication<Application>(
      domain, ae::ObjId{surfaces_demo::ToObjId(
                  surfaces_demo::ObjId::Application)});
  CHECK(application->surfaces->surfaces.size() == 3);
  CHECK(application->surfaces->surfaces[0]->number == 1);
  CHECK(application->surfaces->surfaces[1]->number == 2);
  CHECK(application->surfaces->surfaces[2]->number == 3);

  LinuxApp app2;
  std::thread gui2{[&] { CHECK(app2.Run(dir) == 0); }};
  Window rs1 = None;
  Window rs2 = None;
  Window rs3 = None;
  CHECK(WaitOwned(display, pid, "Surface 1", &rs1, std::chrono::seconds{30}));
  CHECK(WaitOwned(display, pid, "Surface 2", &rs2, std::chrono::seconds{30}));
  CHECK(WaitOwned(display, pid, "Surface 3", &rs3, std::chrono::seconds{30}));
  CHECK(CountOwnedSurfaces(display, pid) == 3);
  constexpr int kTol = 40;
  CHECK(RectNear(ReadOuter(display, rs1), r1, kTol));
  CHECK(RectNear(ReadOuter(display, rs2), r2, kTol));
  CHECK(RectNear(ReadOuter(display, rs3), r3, kTol));

  ClickClose(display, rs2);
  CHECK(WaitCount(display, pid, 2, std::chrono::seconds{30}));
  ClickClose(display, rs1);
  CHECK(WaitCount(display, pid, 1, std::chrono::seconds{30}));
  Window last = FindOwned(display, pid, "Surface 3");
  CHECK(last != None);
  ClickClose(display, last);
  gui2.join();

  DirectoryDomainStorage storage2{dir};
  ae::Domain domain2{storage2};
  auto application2 = LoadApplication<Application>(
      domain2, ae::ObjId{surfaces_demo::ToObjId(
                   surfaces_demo::ObjId::Application)});
  CHECK(application2->surfaces->surfaces.size() == 1);
  CHECK(application2->surfaces->surfaces[0]->number == 3);

  XCloseDisplay(display);
  std::filesystem::remove_all(dir);
}

void ActivateSurface(Display* display, Window window) {
  // Prefer raise + focus + synthetic FocusIn: FocusChange across Display
  // connections is not always delivered to the app client.
  XRaiseWindow(display, window);
  XSetInputFocus(display, window, RevertToParent, CurrentTime);
  XEvent event{};
  event.type = FocusIn;
  event.xfocus.display = display;
  event.xfocus.window = window;
  event.xfocus.mode = NotifyNormal;
  event.xfocus.detail = NotifyNonlinear;
  CHECK(XSendEvent(display, window, False, FocusChangeMask, &event) != 0);
  XFlush(display);
}

Window TopOwnedSurface(Display* display, pid_t pid) {
  Atom stacking = XInternAtom(display, "_NET_CLIENT_LIST_STACKING", True);
  if (stacking != None) {
    Atom actual_type = None;
    int actual_format = 0;
    unsigned long nitems = 0;
    unsigned long bytes_after = 0;
    unsigned char* prop = nullptr;
    int const rc = XGetWindowProperty(
        display, DefaultRootWindow(display), stacking, 0, 1024, False,
        XA_WINDOW, &actual_type, &actual_format, &nitems, &bytes_after, &prop);
    if (rc == Success && prop != nullptr && nitems > 0 && actual_format == 32) {
      auto const* windows = reinterpret_cast<Window const*>(prop);
      for (unsigned long i = nitems; i > 0; --i) {
        Window candidate = windows[i - 1];
        pid_t window_pid = 0;
        if (!ReadPid(display, candidate, &window_pid) || window_pid != pid) {
          continue;
        }
        std::string name;
        if (!ReadName(display, candidate, &name)) {
          continue;
        }
        if (name.rfind("Surface ", 0) == 0) {
          XFree(prop);
          return candidate;
        }
      }
      XFree(prop);
    } else if (prop != nullptr) {
      XFree(prop);
    }
  }
  // Fallback: last mapped owned Surface from tree walk order is weak; return
  // None so the CHECK fails loudly if stacking atom is unavailable.
  return None;
}

void TestActiveZOrderRestored() {
  pid_t const pid = getpid();
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_surfaces_linux_zorder";
  std::filesystem::remove_all(dir);

  EnsureObjectRegistration();
  EnsureSurfacesModelRegistration();
  EnsureLinuxSurfacePresenterRegistration();

  LinuxApp app;
  std::thread gui{[&] { CHECK(app.Run(dir) == 0); }};

  Display* display = XOpenDisplay(nullptr);
  CHECK(display != nullptr);

  Window s1 = None;
  CHECK(WaitOwned(display, pid, "Surface 1", &s1, std::chrono::seconds{30}));
  ClickAdd(display, s1);
  Window s2 = None;
  CHECK(WaitOwned(display, pid, "Surface 2", &s2, std::chrono::seconds{30}));
  ClickAdd(display, s2);
  Window s3 = None;
  CHECK(WaitOwned(display, pid, "Surface 3", &s3, std::chrono::seconds{30}));
  CHECK(WaitCount(display, pid, 3, std::chrono::seconds{10}));

  ActivateSurface(display, s2);
  Pump(display, std::chrono::milliseconds{300});
  CHECK(TopOwnedSurface(display, pid) == s2);

  SendWmDelete(display, s2);
  gui.join();
  CHECK(CountOwnedSurfaces(display, pid) == 0);

  DirectoryDomainStorage storage{dir};
  ae::Domain domain{storage};
  auto application = LoadApplication<Application>(
      domain, ae::ObjId{surfaces_demo::ToObjId(
                  surfaces_demo::ObjId::Application)});
  CHECK(application->surfaces->mobile_current);
  CHECK(application->surfaces->mobile_current->number == 2);

  LinuxApp app2;
  std::thread gui2{[&] { CHECK(app2.Run(dir) == 0); }};
  Window rs1 = None;
  Window rs2 = None;
  Window rs3 = None;
  CHECK(WaitOwned(display, pid, "Surface 1", &rs1, std::chrono::seconds{30}));
  CHECK(WaitOwned(display, pid, "Surface 2", &rs2, std::chrono::seconds{30}));
  CHECK(WaitOwned(display, pid, "Surface 3", &rs3, std::chrono::seconds{30}));
  Pump(display, std::chrono::milliseconds{300});
  CHECK(TopOwnedSurface(display, pid) == rs2);

  SendWmDelete(display, rs2);
  gui2.join();
  XCloseDisplay(display);
  std::filesystem::remove_all(dir);
}

}  // namespace apptraverse::test

int main() {
  CHECK(XInitThreads() != 0);
  XSetErrorHandler(&apptraverse::test::IgnoreBadWindow);
  apptraverse::test::TestPresenterHierarchy();
  apptraverse::test::TestCloseButtonRemovesOne();
  apptraverse::test::TestNativeXKeepsAllSurfaces();
  apptraverse::test::TestActiveZOrderRestored();
  std::cout << "surfaces_linux_smoke_test OK\n";
  return 0;
}
