#ifndef APPTRAVERSE_DESKTOP_SURFACE_PRESENTER_H_
#define APPTRAVERSE_DESKTOP_SURFACE_PRESENTER_H_

#include <cstdint>

#include "apptraverse/object_macros.h"

#include "surfaces_model.h"

namespace apptraverse {

// Desktop one-window-per-Surface presentation layer. Platform siblings:
// Win32SurfacePresenter, MacSurfacePresenter, LinuxSurfacePresenter.
// Not a Node; no journal; no native resources.
class DesktopSurfacePresenter : public SurfacePresenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::DesktopSurfacePresenter",
      DesktopSurfacePresenter, SurfacePresenter, 0)

 protected:
  DesktopSurfacePresenter() = default;

 public:
  explicit DesktopSurfacePresenter(ae::ObjProp prop) : SurfacePresenter{prop} {}

  AE_OBJECT_REFLECT()

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_);
  }

  // GUI-thread: enqueue model Surface::SetDesktopBounds via ObjId proxy.
  void UpdateModelBounds(std::int32_t x, std::int32_t y, std::int32_t width,
                         std::int32_t height);
};

void EnsureDesktopSurfacePresenterRegistration();

}  // namespace apptraverse

#endif  // APPTRAVERSE_DESKTOP_SURFACE_PRESENTER_H_
