#ifndef APPTRAVERSE_EXAMPLE_SURFACES_DEMO_LAYOUT_PUSH_BUTTON_PRESENTER_H_

#define APPTRAVERSE_EXAMPLE_SURFACES_DEMO_LAYOUT_PUSH_BUTTON_PRESENTER_H_



#include "apptraverse/object_macros.h"

#include "apptraverse/platform/windows/win_ui_push_button_presenter.h"



namespace apptraverse::example::surfaces {



// Demo-only: sync layout_* from Surface chrome geometry before base Win presenter.

class SurfacesDemoLayoutPushButtonPresenter

    : public ui::windows::WinPushButtonPresenter {

  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::SurfacesDemoLayoutPushButtonPresenter",
      SurfacesDemoLayoutPushButtonPresenter,
      ui::windows::WinPushButtonPresenter, 0)



 protected:

  SurfacesDemoLayoutPushButtonPresenter() = default;



 public:

  explicit SurfacesDemoLayoutPushButtonPresenter(ae::ObjProp prop)

      : ui::windows::WinPushButtonPresenter{prop} {}



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

  void OnModelChanged() override;



 private:

  void SyncLayoutFieldsFromSurfaceChrome();

};



}  // namespace apptraverse::example::surfaces



#endif  // APPTRAVERSE_EXAMPLE_SURFACES_DEMO_LAYOUT_PUSH_BUTTON_PRESENTER_H_

