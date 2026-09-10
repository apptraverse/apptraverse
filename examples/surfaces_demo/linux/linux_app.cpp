#include "linux_app.h"

#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include "apptraverse/object_serialization.h"
#include "linux_fatal.h"
#include "linux_presenters.h"
#include "surfaces_linux_messages.h"

namespace apptraverse {
namespace {

void SetWmName(Display* display, Window window, char const* name) {
  XStoreName(display, window, name);
  XClassHint hint{};
  hint.res_name = const_cast<char*>("apptraverse_surfaces");
  hint.res_class = const_cast<char*>("AppTraverseSurfaces");
  XSetClassHint(display, window, &hint);
}

}  // namespace

void LinuxApp::OpenDisplay() {
  display_ = XOpenDisplay(nullptr);
  if (display_ == nullptr) {
    FatalLinux("XOpenDisplay");
  }
  root_ = DefaultRootWindow(display_);
  wm_delete_ = XInternAtom(display_, "WM_DELETE_WINDOW", False);
  wm_protocols_ = XInternAtom(display_, "WM_PROTOCOLS", False);
  net_frame_extents_ = XInternAtom(display_, "_NET_FRAME_EXTENTS", False);
}

void LinuxApp::CloseDisplay() {
  if (display_ != nullptr) {
    XCloseDisplay(display_);
    display_ = nullptr;
  }
}

void LinuxApp::CreateWakePipe() {
  if (pipe(wake_pipe_) != 0) {
    FatalLinux("pipe wake");
  }
}

void LinuxApp::CloseWakePipe() {
  if (wake_pipe_[0] >= 0) {
    close(wake_pipe_[0]);
    wake_pipe_[0] = -1;
  }
  if (wake_pipe_[1] >= 0) {
    close(wake_pipe_[1]);
    wake_pipe_[1] = -1;
  }
}

void LinuxApp::WriteWake(std::uint8_t code) {
  for (;;) {
    ssize_t const n = write(wake_pipe_[1], &code, 1);
    if (n == 1) {
      return;
    }
    if (n < 0 && errno == EINTR) {
      continue;
    }
    FatalLinux("write wake pipe");
  }
}

void LinuxApp::RegisterPresenter(Window window,
                                 LinuxSurfacePresenter* presenter) {
  presenters_[window] = presenter;
}

void LinuxApp::UnregisterPresenter(Window window) {
  presenters_.erase(window);
}

LinuxSurfacePresenter* LinuxApp::PresenterFor(Window window) const {
  auto const it = presenters_.find(window);
  if (it == presenters_.end()) {
    return nullptr;
  }
  return it->second;
}

bool LinuxApp::QueryFrameExtents(Window window, long* left, long* right,
                                 long* top, long* bottom) const {
  Atom actual_type = None;
  int actual_format = 0;
  unsigned long nitems = 0;
  unsigned long bytes_after = 0;
  unsigned char* prop = nullptr;
  int const rc = XGetWindowProperty(
      display_, window, net_frame_extents_, 0, 4, False, XA_CARDINAL,
      &actual_type, &actual_format, &nitems, &bytes_after, &prop);
  if (rc != Success || prop == nullptr || nitems < 4 ||
      actual_format != 32) {
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

void LinuxApp::ReadOuterBounds(Window window, int* x, int* y, int* width,
                               int* height) const {
  Window root_return = None;
  Window child = None;
  int win_x = 0;
  int win_y = 0;
  unsigned int win_w = 0;
  unsigned int win_h = 0;
  unsigned int border = 0;
  unsigned int depth = 0;
  if (XGetGeometry(display_, window, &root_return, &win_x, &win_y, &win_w,
                   &win_h, &border, &depth) == 0) {
    FatalLinux("XGetGeometry Surface");
  }
  int root_x = 0;
  int root_y = 0;
  if (XTranslateCoordinates(display_, window, root_, 0, 0, &root_x, &root_y,
                            &child) == False) {
    FatalLinux("XTranslateCoordinates Surface");
  }
  long left = 0;
  long right = 0;
  long top = 0;
  long bottom = 0;
  if (QueryFrameExtents(window, &left, &right, &top, &bottom)) {
    *x = root_x - static_cast<int>(left);
    *y = root_y - static_cast<int>(top);
    *width = static_cast<int>(win_w) + static_cast<int>(left + right);
    *height = static_cast<int>(win_h) + static_cast<int>(top + bottom);
  } else {
    *x = root_x;
    *y = root_y;
    *width = static_cast<int>(win_w);
    *height = static_cast<int>(win_h);
  }
}

void LinuxApp::ApplyOuterPlacement(Window window, int outer_x, int outer_y,
                                   int outer_w, int outer_h) {
  long left = 0;
  long right = 0;
  long top = 0;
  long bottom = 0;
  bool const have_extents =
      QueryFrameExtents(window, &left, &right, &top, &bottom);
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
  XMoveResizeWindow(display_, window, client_x, client_y,
                    static_cast<unsigned int>(client_w),
                    static_cast<unsigned int>(client_h));
  XFlush(display_);
}

void LinuxApp::PlaceOuterWindow(Window window, int x, int y, int width,
                                int height) {
  ApplyOuterPlacement(window, x, y, width, height);
}

void LinuxApp::QueueAllWindowBounds() {
  for (auto const& surface : ui_application_->surfaces->surfaces) {
    LinuxSurfacePresenter::ptr presenter{surface->presenter};
    presenter->QueueCurrentBounds();
  }
}

void LinuxApp::RequestApplicationStop() {
  if (ui_application_) {
    QueueAllWindowBounds();
  }
  session_.RequestStop();
}

void LinuxApp::PostApplicationStop() {
  WriteWake(kLinuxWakeStop);
}

void LinuxApp::CreateLoadingWindow() {
  loading_ = XCreateSimpleWindow(display_, root_, 200, 200, 280, 120, 1,
                                 BlackPixel(display_, DefaultScreen(display_)),
                                 WhitePixel(display_, DefaultScreen(display_)));
  if (loading_ == None) {
    FatalLinux("XCreateSimpleWindow Loading");
  }
  SetWmName(display_, loading_, "Loading");
  XSelectInput(display_, loading_, ExposureMask | StructureNotifyMask);
  XSetWMProtocols(display_, loading_, &wm_delete_, 1);
  XMapWindow(display_, loading_);
  XFlush(display_);
}

void LinuxApp::DestroyLoadingWindow() {
  if (loading_ == None) {
    return;
  }
  XDestroyWindow(display_, loading_);
  loading_ = None;
  XFlush(display_);
}

void LinuxApp::PaintLoading() {
  GC gc = XCreateGC(display_, loading_, 0, nullptr);
  XClearWindow(display_, loading_);
  XDrawString(display_, loading_, gc, 110, 64, "Loading", 7);
  XFreeGC(display_, gc);
  XFlush(display_);
}

void LinuxApp::OnInitialPublished() {
  std::vector<std::uint8_t> bytes;
  {
    std::lock_guard<std::mutex> lock{session_.mu};
    bytes = session_.channel.TakePublishedCopy();
  }
  session_.cv.notify_all();
  ui_domain_ = std::make_unique<ae::Domain>(ui_storage_);
  ByteSource in;
  in.data = bytes.data();
  in.size = bytes.size();
  auto ui_root = LoadInitialPublication(in, *ui_domain_, ui_storage_);
  ui_application_ = Application::ptr::MakeFromThis(
      static_cast<Application*>(ui_root.get()));
  InitializePresenters(*ui_application_, this, &*model_proxy_);
  DestroyLoadingWindow();
}

void LinuxApp::OnIncrementalPublished() {
  std::vector<std::uint8_t> bytes;
  {
    std::lock_guard<std::mutex> lock{session_.mu};
    bytes = session_.channel.TakePublishedCopy();
  }
  session_.cv.notify_all();
  ApplySurfacesStructural(bytes, *ui_application_, ui_storage_, this,
                          &*model_proxy_);
}

void LinuxApp::DrainWake() {
  for (;;) {
    std::uint8_t code = 0;
    ssize_t const n = read(wake_pipe_[0], &code, 1);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return;
      }
      FatalLinux("read wake pipe");
    }
    if (n == 0) {
      return;
    }
    if (code == kLinuxWakeInitialPublished) {
      OnInitialPublished();
    } else if (code == kLinuxWakeIncrementalPublished) {
      OnIncrementalPublished();
    } else if (code == kLinuxWakeStop) {
      RequestApplicationStop();
    }
  }
}

void LinuxApp::DispatchXEvent(XEvent const& event) {
  if (event.type == ClientMessage) {
    if (event.xclient.message_type == wm_protocols_ &&
        static_cast<Atom>(event.xclient.data.l[0]) == wm_delete_) {
      if (event.xclient.window == loading_) {
        // Startup is not cancelable until Loading is gone.
        return;
      }
      // Native WM close of any Surface → whole-application stop. Never Remove.
      RequestApplicationStop();
      return;
    }
  }
  if (event.type == Expose && event.xexpose.window == loading_ &&
      event.xexpose.count == 0) {
    PaintLoading();
    return;
  }
  LinuxSurfacePresenter* presenter = PresenterFor(event.xany.window);
  if (presenter != nullptr) {
    presenter->HandleEvent(event);
  }
}

int LinuxApp::Run(std::filesystem::path const& state_dir) {
  EnsureLinuxSurfacePresenterRegistration();
  OpenDisplay();
  CreateWakePipe();

  // Non-blocking wake reads so DrainWake can empty the pipe after poll.
  int flags = fcntl(wake_pipe_[0], F_GETFL, 0);
  if (flags < 0 || fcntl(wake_pipe_[0], F_SETFL, flags | O_NONBLOCK) < 0) {
    FatalLinux("fcntl wake O_NONBLOCK");
  }

  session_.state_dir = state_dir;
  model_proxy_.emplace([this](ModelObjectProxy::ModelWork work) {
    session_.Post(std::move(work));
  });

  CreateLoadingWindow();

  model_done_.store(false);
  model_thread_ = std::thread([this] {
    session_.Run([this](SurfacesPublicationKind kind) {
      WriteWake(kind == SurfacesPublicationKind::Initial
                    ? kLinuxWakeInitialPublished
                    : kLinuxWakeIncrementalPublished);
    });
    model_done_.store(true);
    WriteWake(kLinuxWakeStop);
  });

  int const xfd = ConnectionNumber(display_);
  while (!model_done_.load()) {
    while (XPending(display_) > 0) {
      XEvent event{};
      XNextEvent(display_, &event);
      DispatchXEvent(event);
    }
    pollfd fds[2]{};
    fds[0].fd = xfd;
    fds[0].events = POLLIN;
    fds[1].fd = wake_pipe_[0];
    fds[1].events = POLLIN;
    int const pr = poll(fds, 2, -1);
    if (pr < 0) {
      if (errno == EINTR) {
        continue;
      }
      FatalLinux("poll");
    }
    if (fds[1].revents & (POLLIN | POLLHUP)) {
      DrainWake();
    }
  }

  // Drain remaining X/wake after model finished (final STOP already applied).
  DrainWake();
  while (XPending(display_) > 0) {
    XEvent event{};
    XNextEvent(display_, &event);
    DispatchXEvent(event);
  }

  model_thread_.join();
  if (ui_application_) {
    UnloadPresenters(*ui_application_);
  }
  ui_application_ = {};
  ui_domain_.reset();
  model_proxy_.reset();
  DestroyLoadingWindow();
  CloseWakePipe();
  CloseDisplay();
  return 0;
}

}  // namespace apptraverse
