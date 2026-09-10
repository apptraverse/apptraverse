#ifndef APPTRAVERSE_MOBILE_SURFACE_PRESENTER_H_
#define APPTRAVERSE_MOBILE_SURFACE_PRESENTER_H_

#include "apptraverse/object_macros.h"

#include "surfaces_model.h"

namespace apptraverse {

// Mobile one-page-per-Surface presentation layer. Platform siblings:
// AndroidSurfacePresenter, IOSSurfacePresenter. Not a Node; no journal; no
// native resources. The current page is presentation state and never reaches
// the model; desktop bounds on Surface are ignored here.
class MobileSurfacePresenter : public SurfacePresenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::MobileSurfacePresenter",
      MobileSurfacePresenter, SurfacePresenter, 0)

 protected:
  MobileSurfacePresenter() = default;

 public:
  explicit MobileSurfacePresenter(ae::ObjProp prop) : SurfacePresenter{prop} {}

  AE_OBJECT_REFLECT()

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_);
  }
};

void EnsureMobileSurfacePresenterRegistration();

}  // namespace apptraverse

#endif  // APPTRAVERSE_MOBILE_SURFACE_PRESENTER_H_
