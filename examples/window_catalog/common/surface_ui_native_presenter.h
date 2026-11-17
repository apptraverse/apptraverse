#ifndef APPTRAVERSE_EXAMPLES_SURFACE_UI_NATIVE_PRESENTER_H_
#define APPTRAVERSE_EXAMPLES_SURFACE_UI_NATIVE_PRESENTER_H_
#include "surface_ui_model.h"
#include "apptraverse/model_object_proxy.h"
namespace apptraverse::example::surface_ui {
// Only application input adaptation. Native measurement, geometry, settings and
// lifetime are provided by the ordinary platform window presenter.
template <class Native> class SurfacePresenterMixin : public Native {
 public:
  explicit SurfacePresenterMixin(ae::ObjProp p) : Native{p} {}
  void NotifyActivatedFromUi() override {
    this->model_proxy->Invoke(ApplicationId(), &SurfaceApplication::Select, this->window.id());
  }
 protected:
  SurfacePresenterMixin() = default;
};
}
#endif
