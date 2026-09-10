#include "android_surface_presenter.h"

#include "android_log.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(AndroidSurfacePresenter);

}  // namespace

void EnsureAndroidSurfacePresenterRegistration() {
  EnsureMobileSurfacePresenterRegistration();
  (void)&g_apptraverse_registrar_AndroidSurfacePresenter;
}

void AndroidSurfacePresenter::OnLoad() {
  android::LogMarker("SURFACE_PAGE_LOADED number=" +
                     std::to_string(surface->number) + " id=" +
                     std::to_string(surface->obj_id.id()));
}

void AndroidSurfacePresenter::OnUnload() {
  android::LogMarker("SURFACE_PAGE_UNLOADED number=" +
                     std::to_string(surface->number) + " id=" +
                     std::to_string(surface->obj_id.id()));
}

}  // namespace apptraverse
