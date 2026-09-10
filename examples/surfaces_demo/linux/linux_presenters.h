#ifndef APPTRAVERSE_SURFACES_LINUX_PRESENTERS_H_
#define APPTRAVERSE_SURFACES_LINUX_PRESENTERS_H_

#include <X11/Xlib.h>

#include <cstdint>

#include "apptraverse/object_macros.h"

#include "desktop_surface_presenter.h"

namespace apptraverse {

class LinuxApp;

void EnsureLinuxSurfacePresenterRegistration();

class LinuxSurfacePresenter : public DesktopSurfacePresenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::LinuxSurfacePresenter",
      LinuxSurfacePresenter, DesktopSurfacePresenter, 0)

 protected:
  LinuxSurfacePresenter() = default;

 public:
  explicit LinuxSurfacePresenter(ae::ObjProp prop)
      : DesktopSurfacePresenter{prop} {}

  AE_OBJECT_REFLECT()

  void OnLoad() override;
  void OnModelChanged() override;
  void OnUnload() override;
  bool OnCommand(std::uint32_t command_id,
                 std::uint16_t notification_code) override;

  // Snapshot outer frame (best-effort via _NET_FRAME_EXTENTS) and enqueue
  // model bounds update through DesktopSurfacePresenter.
  void QueueCurrentBounds();

  // Dispatch one X event that targets this Surface window.
  void HandleEvent(XEvent const& event);

  Window window{None};
  LinuxApp* app{nullptr};

  // Client-area hit targets for the two drawn buttons (no toolkit widgets).
  int add_x{12};
  int add_y{12};
  int add_w{80};
  int add_h{28};
  int close_x{100};
  int close_y{12};
  int close_w{160};
  int close_h{28};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_SURFACES_LINUX_PRESENTERS_H_
