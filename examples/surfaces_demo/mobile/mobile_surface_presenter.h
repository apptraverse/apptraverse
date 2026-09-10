#ifndef APPTRAVERSE_MOBILE_SURFACE_PRESENTER_H_
#define APPTRAVERSE_MOBILE_SURFACE_PRESENTER_H_

#include <string>

#include "apptraverse/object_macros.h"

#include "surfaces_model.h"

namespace apptraverse {

// Mobile one-page-per-Surface presentation layer. Platform siblings:
// AndroidSurfacePresenter, IOSSurfacePresenter. Not a Node; no journal; no
// native resources. Desktop bounds on Surface are ignored here.
//
// Canonical persisted current page is Surfaces::mobile_current (Surface
// identity). Hosts report the visible page via SurfacePresenter::PageShown.
// Runtime pager index is presentation-only.
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

  // Page caption for this Surface (iOS / shared mobile UI).
  std::string PageTitle() const;

  // Last remaining page stays: Remove current is presentation-disabled.
  // Model Remove is still available via proxy when RemovableFromPager is true.
  bool RemovableFromPager() const;
};

void EnsureMobileSurfacePresenterRegistration();

}  // namespace apptraverse

#endif  // APPTRAVERSE_MOBILE_SURFACE_PRESENTER_H_
