#ifndef APPTRAVERSE_SURFACES_IOS_PRESENTERS_H_
#define APPTRAVERSE_SURFACES_IOS_PRESENTERS_H_

#include "apptraverse/object_macros.h"

#include "mobile_surface_presenter.h"

namespace apptraverse {

// One Surface page: a UIKit container filled with SwiftUI content. OnLoad
// creates the container UIView and registers it with the single mobile host; it
// never creates a UIWindow. Page order and the current page belong to the host,
// not here.
class IOSSurfacePresenter : public MobileSurfacePresenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::IOSSurfacePresenter",
      IOSSurfacePresenter, MobileSurfacePresenter, 0)

 protected:
  IOSSurfacePresenter() = default;

 public:
  explicit IOSSurfacePresenter(ae::ObjProp prop)
      : MobileSurfacePresenter{prop} {}

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

  // Runtime-only UIView container, retained while this page is presented.
  void* page_view{nullptr};
};

void EnsureIOSSurfacePresenterRegistration();

// presentation_host → IOSApp pager topology (defined in ios_app.mm). The page
// UIView is added to / removed from the host scroll view; the host decides the
// order and the current page.
void IOSAttachPage(void* presentation_host, void* page_view);
void IOSDetachPage(void* page_view);

}  // namespace apptraverse

#endif  // APPTRAVERSE_SURFACES_IOS_PRESENTERS_H_
