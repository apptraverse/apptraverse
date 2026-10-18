#include "surface_window_presenter.h"

#include "apptraverse/model_object_proxy.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(SurfaceWindowPresenter);

}  // namespace

void EnsureSurfaceWindowPresenterRegistration() {
  (void)&g_apptraverse_registrar_SurfaceWindowPresenter;
}

void SurfaceWindowPresenter::PageShown() {
  model_proxy->Invoke<Surface>(surface->obj_id, &Surface::MakeCurrent);
}

void SurfaceWindowPresenter::PresentationSizeChanged(std::int32_t width,
                                                     std::int32_t height) {
  model_proxy->Invoke<Surface>(surface->obj_id, &Surface::SetPresentationSize,
                               width, height);
}

}  // namespace apptraverse
