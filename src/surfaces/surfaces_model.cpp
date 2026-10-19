#include "apptraverse/surfaces/surfaces_model.h"

#include <algorithm>
#include <cassert>
#include <unordered_set>

#include "apptraverse/model_object_proxy.h"
#include "apptraverse/runtime_node.h"

#include "apptraverse/surfaces/surfaces_ids.h"
#include "apptraverse/surfaces/surfaces_presentation.h"
#include "apptraverse/surfaces/surfaces_presentation_size_queue.h"

namespace apptraverse {

void Surfaces::Apply(AddSurfaceEvent const& event) {
  surfaces.push_back(event.surface);
  desktop_z_order.push_back(event.surface);
  NoteMaterializedChange();
}

void Surfaces::Apply(RemoveSurfaceEvent const& event) {
  auto const it = std::find_if(
      surfaces.begin(), surfaces.end(),
      [&](Surface::ptr const& entry) { return &*entry == &*event.surface; });
  assert(it != surfaces.end() &&
         "RemoveSurfaceEvent surface must be live on Apply");
  surfaces.erase(it);
  if (mobile_current && &*mobile_current == &*event.surface) {
    // Which page becomes current afterwards is presentation policy; the model
    // only drops the reference to the removed Surface.
    mobile_current = {};
  }
  desktop_z_order.erase(
      std::remove_if(desktop_z_order.begin(), desktop_z_order.end(),
                     [&](Surface::ptr const& entry) {
                       return &*entry == &*event.surface;
                     }),
      desktop_z_order.end());
  NoteMaterializedChange();
}

void Surfaces::Apply(SetCurrentSurfaceEvent const& event) {
  mobile_current = event.surface;
  NoteMaterializedChange();
}

void Surfaces::Apply(SetDesktopZOrderEvent const& event) {
  desktop_z_order = event.ordered;
  NoteMaterializedChange();
}

namespace {

bool SameSurfaceOrder(std::vector<Surface::ptr> const& a,
                      std::vector<Surface::ptr> const& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i]->obj_id != b[i]->obj_id) {
      return false;
    }
  }
  return true;
}

Surface::ptr FindLiveSurfaceByObjId(Surfaces& parent, ae::ObjId id) {
  for (auto const& entry : parent.surfaces) {
    if (entry->obj_id == id) {
      return entry;
    }
  }
  return {};
}

}  // namespace

void Surfaces::SetDesktopZOrderFromObjIds(
    std::vector<std::uint32_t> ordered_obj_ids) {
  std::vector<Surface::ptr> resolved;
  resolved.reserve(ordered_obj_ids.size());
  std::unordered_set<std::uint32_t> seen;
  for (std::uint32_t raw_id : ordered_obj_ids) {
    if (!seen.insert(raw_id).second) {
      continue;
    }
    Surface::ptr surface = FindLiveSurfaceByObjId(*this, ae::ObjId{raw_id});
    if (!surface) {
      continue;
    }
    resolved.push_back(std::move(surface));
  }
  for (auto const& live : surfaces) {
    bool already = false;
    for (auto const& entry : resolved) {
      if (entry->obj_id == live->obj_id) {
        already = true;
        break;
      }
    }
    if (!already) {
      resolved.push_back(live);
    }
  }
  if (SameSurfaceOrder(desktop_z_order, resolved)) {
    return;
  }
  auto event =
      SetDesktopZOrderEvent::ptr::Create(ae::CreateWith{*domain});
  event->ordered = std::move(resolved);
  Commit(event);
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

void Surface::AddSurface() {
  Surfaces& parent = *surfaces;

  auto sibling = Surface::ptr::Create(ae::CreateWith{*parent.domain});
  std::uint32_t next_number = 1;
  for (auto const& existing : parent.surfaces) {
    if (existing->number >= next_number) {
      next_number = existing->number + 1;
    }
  }
  sibling->number = next_number;
  AssignInitialDesktopBounds(*sibling);
  sibling->surfaces = surfaces;
  assert(parent.host_application != nullptr &&
         "Surfaces.host_application must reference the owning Application");
  Application& application = *parent.host_application;
  if (parent.platform_attach != nullptr) {
    parent.platform_attach(*sibling, application, *parent.domain);
  }
  // New Surface is a Node: establish base/journal and inherit this Surface's
  // model-runtime notifier before it becomes live.
  InitializeRuntimeNode(*sibling, *this);

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

void Surface::MakeCurrent() {
  Surfaces& parent = *surfaces;
  if (parent.mobile_current && &*parent.mobile_current == this) {
    return;
  }
  auto const it =
      std::find_if(parent.surfaces.begin(), parent.surfaces.end(),
                   [&](Surface::ptr const& entry) { return &*entry == this; });
  if (it == parent.surfaces.end()) {
    // Historical / already-removed Surface: a page id from a replaced page
    // list is a stale no-op.
    return;
  }
  auto event =
      SetCurrentSurfaceEvent::ptr::Create(ae::CreateWith{*parent.domain});
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

void SurfacePresenter::AddClick() {
  model_proxy->Invoke<Surface>(surface->obj_id, &Surface::AddSurface);
}

void SurfacePresenter::RemoveClick() {
  model_proxy->Invoke<Surface>(surface->obj_id, &Surface::Remove);
}

void SurfacePresenter::PageShown() {
  model_proxy->Invoke<Surface>(surface->obj_id, &Surface::MakeCurrent);
}

void SurfacePresenter::PresentationSizeChanged(std::int32_t width,
                                               std::int32_t height) {
  PostSurfacePresentationSize(model_proxy, surface->obj_id, width, height);
}

}  // namespace apptraverse
