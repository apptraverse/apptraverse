#ifndef APPTRAVERSE_ANDROID_SURFACE_PRESENTER_H_
#define APPTRAVERSE_ANDROID_SURFACE_PRESENTER_H_

#include "apptraverse/object_macros.h"

#include "mobile_surface_presenter.h"

namespace apptraverse {

// Android page for one Surface. Owns no Activity, no View and no JNI
// reference: the pager is rebuilt from the page list the native runtime
// publishes after every presenter update. Add / Remove current arrive here
// through the inherited SurfacePresenter actions.
class AndroidSurfacePresenter : public MobileSurfacePresenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::AndroidSurfacePresenter",
      AndroidSurfacePresenter, MobileSurfacePresenter, 0)

 protected:
  AndroidSurfacePresenter() = default;

 public:
  explicit AndroidSurfacePresenter(ae::ObjProp prop)
      : MobileSurfacePresenter{prop} {}

  AE_OBJECT_REFLECT()

  void OnLoad() override;
  void OnUnload() override;
};

void EnsureAndroidSurfacePresenterRegistration();

}  // namespace apptraverse

#endif  // APPTRAVERSE_ANDROID_SURFACE_PRESENTER_H_
