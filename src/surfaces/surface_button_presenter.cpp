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
  return window_presenter && window_presenter->presentation_loaded;
}

bool SurfaceButtonPresenter::OnCommand(std::uint32_t command_id,
                                       std::uint16_t notification_code) {
  (void)notification_code;
  if (command_id != control_id || !model_proxy || !surface) {
    return false;
  }
  if (role == SurfaceButtonRole::Add) {
    model_proxy->Invoke<Surface>(surface->obj_id, &Surface::AddSurface);
    return true;
  }
  model_proxy->Invoke<Surface>(surface->obj_id, &Surface::Remove);
  return true;
}

}  // namespace apptraverse
