#include "apptraverse/surfaces/surfaces_registration.h"

#include "apptraverse/object_serialization.h"
#include "apptraverse/object_macros.h"

#include "aether-objects/obj/registry.h"

#include "apptraverse/surfaces/desktop_surface_presenter.h"
#include "apptraverse/surfaces/surface_button_presenter.h"
#include "apptraverse/surfaces/surface_language_button_presenter.h"
#include "apptraverse/surfaces/surface_window_presenter.h"
#include "apptraverse/surfaces/surfaces_model.h"

namespace apptraverse {
namespace {

void AppendSurfaceLiveGraphEdges(ae::Obj& object, ae::Obj& walk_root,
                                 std::vector<ae::Obj*>& stack) {
  if (ae::Registry::GetRegistry().GenerationDistance(
          Surface::kClassId, object.GetClassId()) < 0) {
    return;
  }
  auto* surface = static_cast<Surface*>(&object);
  auto push = [&](auto& pointer) {
    if (!pointer.is_valid()) {
      return;
    }
    if (auto found = walk_root.domain->Find(pointer.id())) {
      stack.push_back(&*found);
    }
  };
  push(surface->chrome.window);
  push(surface->chrome.add_button);
  push(surface->chrome.close_button);
  push(surface->chrome.language_button);
}

APPTRAVERSE_REGISTER(Surface);
APPTRAVERSE_REGISTER(SurfacePresenter);
APPTRAVERSE_REGISTER(Surfaces);
APPTRAVERSE_REGISTER(AddSurfaceEvent);
APPTRAVERSE_REGISTER(RemoveSurfaceEvent);
APPTRAVERSE_REGISTER(SetCurrentSurfaceEvent);
APPTRAVERSE_REGISTER(SurfaceBoundsChangedEvent);
APPTRAVERSE_REGISTER(SurfacePresentationSizeChangedEvent);
APPTRAVERSE_REGISTER(Application);

}  // namespace

void EnsureSurfacesRegistration() {
  RegisterLiveGraphEdgeAppender(&AppendSurfaceLiveGraphEdges);
  (void)&g_apptraverse_registrar_Application;
  (void)&g_apptraverse_registrar_Surface;
  (void)&g_apptraverse_registrar_SurfaceBoundsChangedEvent;
  (void)&g_apptraverse_registrar_SurfacePresentationSizeChangedEvent;
  EnsureDesktopSurfacePresenterRegistration();
  EnsureSurfaceWindowPresenterRegistration();
  EnsureSurfaceButtonPresenterRegistration();
  EnsureSurfaceLanguageButtonPresenterRegistration();
}

}  // namespace apptraverse
