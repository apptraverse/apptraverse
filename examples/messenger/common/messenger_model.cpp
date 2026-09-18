#include "messenger_model.h"

#include "apptraverse/model_object_proxy.h"
#include "apptraverse/object_macros.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(Surface);
APPTRAVERSE_REGISTER(SurfacePresenter);
APPTRAVERSE_REGISTER(Surfaces);
APPTRAVERSE_REGISTER(SurfaceBoundsChangedEvent);
APPTRAVERSE_REGISTER(SurfacePresentationSizeChangedEvent);
APPTRAVERSE_REGISTER(Application);

}  // namespace

void EnsureMessengerModelRegistration() {
  (void)&g_apptraverse_registrar_Application;
  (void)&g_apptraverse_registrar_Surface;
  (void)&g_apptraverse_registrar_SurfaceBoundsChangedEvent;
  (void)&g_apptraverse_registrar_SurfacePresentationSizeChangedEvent;
}

void Surface::Apply(SurfaceBoundsChangedEvent const& event) {
  desktop_x = event.x;
  desktop_y = event.y;
  desktop_width = event.width;
  desktop_height = event.height;
  NoteMaterializedChange();
}

void Surface::Apply(SurfacePresentationSizeChangedEvent const& event) {
  presentation_width = event.width;
  presentation_height = event.height;
  NoteMaterializedChange();
}

void Surface::SetDesktopBounds(std::int32_t x, std::int32_t y,
                               std::int32_t width, std::int32_t height) {
  if (desktop_x == x && desktop_y == y && desktop_width == width &&
      desktop_height == height) {
    return;
  }
  auto event =
      SurfaceBoundsChangedEvent::ptr::Create(ae::CreateWith{*domain});
  event->x = x;
  event->y = y;
  event->width = width;
  event->height = height;
  Commit(event);
}

void Surface::SetPresentationSize(std::int32_t width, std::int32_t height) {
  if (presentation_width == width && presentation_height == height) {
    return;
  }
  auto event = SurfacePresentationSizeChangedEvent::ptr::Create(
      ae::CreateWith{*domain});
  event->width = width;
  event->height = height;
  Commit(event);
}

void SurfacePresenter::PresentationSizeChanged(std::int32_t width,
                                               std::int32_t height) {
  model_proxy->Invoke<Surface>(surface->obj_id, &Surface::SetPresentationSize,
                               width, height);
}

}  // namespace apptraverse
