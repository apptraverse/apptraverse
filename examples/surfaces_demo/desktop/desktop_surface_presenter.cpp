#include "desktop_surface_presenter.h"

#include "apptraverse/model_object_proxy.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(DesktopSurfacePresenter);

}  // namespace

void EnsureDesktopSurfacePresenterRegistration() {
  (void)&g_apptraverse_registrar_DesktopSurfacePresenter;
}

void DesktopSurfacePresenter::UpdateModelBounds(std::int32_t x, std::int32_t y,
                                                std::int32_t width,
                                                std::int32_t height) {
  model_proxy->Invoke<Surface>(surface->obj_id, &Surface::SetDesktopBounds, x,
                               y, width, height);
}

}  // namespace apptraverse
