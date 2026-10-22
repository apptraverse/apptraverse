#ifndef APPTRAVERSE_EXAMPLE_SURFACES_DEMO_SURFACE_WINDOW_HOST_H_
#define APPTRAVERSE_EXAMPLE_SURFACES_DEMO_SURFACE_WINDOW_HOST_H_

#include "apptraverse/object_macros.h"
#include "apptraverse/platform/windows/surfaces_win_presenters.h"
#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::example::surfaces {

// UiWindowPresenter adapter: native child parent is an existing Surface window.
class SurfacesDemoSurfaceWindowHostPresenter : public ui::UiWindowPresenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::SurfacesDemoSurfaceWindowHostPresenter",
      SurfacesDemoSurfaceWindowHostPresenter, ui::UiWindowPresenter, 0)

 protected:
  SurfacesDemoSurfaceWindowHostPresenter() = default;

 public:
  explicit SurfacesDemoSurfaceWindowHostPresenter(ae::ObjProp prop)
      : ui::UiWindowPresenter{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(surface_window))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, surface_window);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, surface_window);
  }

  Win32SurfaceWindowPresenter::ptr surface_window;

  bool ReadyForPresentation() const override;
  bool PresentationCreateSucceeded() const override;
  void* NativeChildParentHandle() const override;
  ui::UiControlLayout ResolveChildLayout(
      ui::UiControlLayout requested) const override;

  void OnLoad() override;
  void OnUnload() override;
};

}  // namespace apptraverse::example::surfaces

#endif  // APPTRAVERSE_EXAMPLE_SURFACES_DEMO_SURFACE_WINDOW_HOST_H_
