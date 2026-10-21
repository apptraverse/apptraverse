#include "apptraverse/surfaces/surface_button_presenter.h"

#include "apptraverse/model_object_proxy.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(SurfaceButtonPresenter);

}  // namespace

void EnsureSurfaceButtonPresenterRegistration() {
  (void)&g_apptraverse_registrar_SurfaceButtonPresenter;
}

bool SurfaceButtonPresenter::ReadyForPresentation() const {
  return window_presenter->presentation_loaded;
}

void SurfaceButtonPresenter::NotifyClickedFromUi() {
  if (role == SurfaceButtonRole::Add) {
    model_proxy->Invoke<Surface>(surface->obj_id, &Surface::AddSurface);
    return;
  }
  model_proxy->Invoke<Surface>(surface->obj_id, &Surface::Remove);
}

}  // namespace apptraverse
