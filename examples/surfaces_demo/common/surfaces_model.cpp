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
APPTRAVERSE_REGISTER(Application);

}  // namespace

void EnsureSurfacesModelRegistration() {
  (void)&g_apptraverse_registrar_Application;
  (void)&g_apptraverse_registrar_Surface;
}

void Surfaces::Apply(AddSurfaceEvent const& event) {
  assert(event.surface.is_valid());
  assert(event.surface.is_loaded());
  assert(event.surface->surfaces.is_valid());
  assert(&*event.surface->surfaces == this);
  surfaces.push_back(event.surface);
  NoteMaterializedChange();
}

void Surfaces::Apply(RemoveSurfaceEvent const& event) {
  assert(event.surface.is_valid());
  assert(event.surface.is_loaded());
  auto const it = std::find_if(
      surfaces.begin(), surfaces.end(), [&](Surface::ptr const& entry) {
        return entry.is_valid() && &*entry == &*event.surface;
      });
  assert(it != surfaces.end() &&
         "RemoveSurfaceEvent surface must be live on Apply");
  surfaces.erase(it);
  NoteMaterializedChange();
}

void Surface::AddSurface() {
  Surfaces& parent = *surfaces;
  assert(parent.domain != nullptr);

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
  assert(parent.domain != nullptr);
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

void SurfacePresenter::AddClick() {
  assert(model_proxy != nullptr);
  model_proxy->Invoke<Surface>(surface->obj_id, &Surface::AddSurface);
}

void SurfacePresenter::RemoveClick() {
  assert(model_proxy != nullptr);
  model_proxy->Invoke<Surface>(surface->obj_id, &Surface::Remove);
}

}  // namespace apptraverse
