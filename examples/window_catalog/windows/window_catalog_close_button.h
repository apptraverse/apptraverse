#ifndef APPTRAVERSE_EXAMPLE_SURFACES_DEMO_CLOSE_BUTTON_H_
#define APPTRAVERSE_EXAMPLE_SURFACES_DEMO_CLOSE_BUTTON_H_

#include "apptraverse/object_macros.h"
#include "apptraverse/surfaces/surfaces_model.h"
#include "apptraverse/ui/ui_push_button.h"

namespace apptraverse::example::surfaces {

class SurfacesDemoCloseButton : public ui::UiPushButton {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::SurfacesDemoCloseButton",
      SurfacesDemoCloseButton, ui::UiPushButton, 0)

 protected:
  SurfacesDemoCloseButton() = default;

 public:
  explicit SurfacesDemoCloseButton(ae::ObjProp prop) : ui::UiPushButton{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(surface))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, surface);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, surface);
  }

  Surface::ptr surface;

  void HandlePushButtonClicked() override;
};

}  // namespace apptraverse::example::surfaces

#endif  // APPTRAVERSE_EXAMPLE_SURFACES_DEMO_CLOSE_BUTTON_H_
