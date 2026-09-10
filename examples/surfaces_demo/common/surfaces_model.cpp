#include "surfaces_model.h"

#include <algorithm>
#include <cassert>

#include "apptraverse/model_object_proxy.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/runtime_node.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(Surface);
APPTRAVERSE_REGISTER(SurfacePresenter);
APPTRAVERSE_REGISTER(Surfaces);
APPTRAVERSE_REGISTER(AddSurfaceEvent);
APPTRAVERSE_REGISTER(RemoveSurfaceEvent);
APPTRAVERSE_REGISTER(SurfaceBoundsChangedEvent);
APPTRAVERSE_REGISTER(Application);

}  // namespace

void EnsureSurfacesModelRegistration() {
  (void)&g_apptraverse_registrar_Application;
  (void)&g_apptraverse_registrar_Surface;
  (void)&g_apptraverse_registrar_SurfaceBoundsChangedEvent;
}

void Surfaces::Apply(AddSurfaceEvent const& event) {
  surfaces.push_back(event.surface);
  NoteMaterializedChange();
}

void Surfaces::Apply(RemoveSurfaceEvent const& event) {
  auto const it = std::find_if(
      surfaces.begin(), surfaces.end(),
      [&](Surface::ptr const& entry) { return &*entry == &*event.surface; });
  assert(it != surfaces.end() &&
         "RemoveSurfaceEvent surface must be live on Apply");
  surfaces.erase(it);
  NoteMaterializedChange();
}

void Surface::Apply(SurfaceBoundsChangedEvent const& event) {
  desktop_x = event.x;
  desktop_y = event.y;
  desktop_width = event.width;
  desktop_height = event.height;
  NoteMaterializedChange();
}

void Surface::AddSurface() {
  Surfaces& parent = *surfaces;

  auto sibling = Surface::ptr::Create(ae::CreateWith{*parent.domain});
  auto sibling_presenter =
      SurfacePresenter::ptr::Create(ae::CreateWith{*parent.domain});
  std::uint32_t next_number = 1;
  for (auto const& existing : parent.surfaces) {
    if (existing->number >= next_number) {
      next_number = existing->number + 1;
    }
  }
  sibling->number = next_number;
  AssignInitialDesktopBounds(*sibling);
  sibling->surfaces = surfaces;
  sibling->presenter = sibling_presenter;
  sibling_presenter->surface = sibling;
  // New Surface is a Node: establish base/journal before it becomes live.
  InitializeRuntimeNode(*sibling);

  auto event = AddSurfaceEvent::ptr::Create(ae::CreateWith{*parent.domain});
  event->surface = sibling;
  parent.Commit(event);
}

void Surface::Remove() {
  Surfaces& parent = *surfaces;
  auto const it =
      std::find_if(parent.surfaces.begin(), parent.surfaces.end(),
                   [&](Surface::ptr const& entry) { return &*entry == this; });
  if (it == parent.surfaces.end()) {
    // Historical / already-removed Surface: stale double-remove is a no-op.
    return;
  }
  auto event =
      RemoveSurfaceEvent::ptr::Create(ae::CreateWith{*parent.domain});
  event->surface = Surface::ptr::MakeFromThis(this);
  parent.Commit(event);
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

void SurfacePresenter::AddClick() {
  model_proxy->Invoke<Surface>(surface->obj_id, &Surface::AddSurface);
}

void SurfacePresenter::RemoveClick() {
  model_proxy->Invoke<Surface>(surface->obj_id, &Surface::Remove);
}

}  // namespace apptraverse
