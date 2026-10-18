#ifndef APPTRAVERSE_SURFACE_BUTTON_PRESENTER_H_
#define APPTRAVERSE_SURFACE_BUTTON_PRESENTER_H_

#include "apptraverse/object_macros.h"
#include "apptraverse/presenter.h"

#include "surface_window_presenter.h"
#include "surfaces_model.h"

namespace apptraverse {

enum class SurfaceButtonRole : std::uint8_t {
  Add = 0,
  Close = 1,
};

// One presenter object per Surface push button. Platform siblings:
// Win32SurfaceButtonPresenter (native BUTTON on Windows).
class SurfaceButtonPresenter : public Presenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::SurfaceButtonPresenter",
      SurfaceButtonPresenter, Presenter, 0)

 protected:
  SurfaceButtonPresenter() = default;

 public:
  explicit SurfaceButtonPresenter(ae::ObjProp prop) : Presenter{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(surface), AE_MMBR(window_presenter),
                    AE_MMBR(role), AE_MMBR(control_id))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, surface, window_presenter, role, control_id);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, surface, window_presenter, role, control_id);
  }

  bool ReadyForPresentation() const override;
  bool OnCommand(std::uint32_t command_id,
                 std::uint16_t notification_code) override;

  Surface::ptr surface;
  ae::ObjPtr<SurfaceWindowPresenter> window_presenter;
  SurfaceButtonRole role{SurfaceButtonRole::Add};
  std::uint32_t control_id{0};
};

void EnsureSurfaceButtonPresenterRegistration();

}  // namespace apptraverse

#endif  // APPTRAVERSE_SURFACE_BUTTON_PRESENTER_H_
