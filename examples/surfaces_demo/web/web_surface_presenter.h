#ifndef APPTRAVERSE_WEB_SURFACE_PRESENTER_H_
#define APPTRAVERSE_WEB_SURFACE_PRESENTER_H_

#include "apptraverse/object_macros.h"

#include "surfaces_model.h"

namespace apptraverse {

// Browser DOM presentation for one Surface. Sibling of Desktop/Mobile
// branches: derives from SurfacePresenter directly. Not a Node; no journal;
// no desktop geometry; no model pointer stored for JavaScript.
class WebSurfacePresenter : public SurfacePresenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::WebSurfacePresenter",
      WebSurfacePresenter, SurfacePresenter, 0)

 protected:
  WebSurfacePresenter() = default;

 public:
  explicit WebSurfacePresenter(ae::ObjProp prop) : SurfacePresenter{prop} {}

  AE_OBJECT_REFLECT()

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_);
  }

  void OnLoad() override;
  void OnUnload() override;
};

void EnsureWebSurfacePresenterRegistration();

}  // namespace apptraverse

#endif  // APPTRAVERSE_WEB_SURFACE_PRESENTER_H_
