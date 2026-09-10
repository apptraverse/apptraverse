#ifndef APPTRAVERSE_MOBILE_SURFACE_PRESENTER_H_
#define APPTRAVERSE_MOBILE_SURFACE_PRESENTER_H_

#include <string>

#include "apptraverse/object_macros.h"

#include "surfaces_model.h"

namespace apptraverse {

// Mobile pager presentation layer: one application, one native host, every
// Surface is a page. Platform siblings: IOSSurfacePresenter, later Android.
// Not a Node; no journal; no native types. Mobile ignores Surface::desktop_*.
//
// The current page is host runtime state, not model state: nothing here is
// reflected or persisted.
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

  // Page caption for this Surface.
  std::string PageTitle() const;

  // Mobile pager policy: the last remaining page stays. Presentation-only —
  // the model has no notion of a removable or current Surface.
  bool RemovableFromPager() const;
};

void EnsureMobileSurfacePresenterRegistration();

}  // namespace apptraverse

#endif  // APPTRAVERSE_MOBILE_SURFACE_PRESENTER_H_
