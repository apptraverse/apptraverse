#ifndef APPTRAVERSE_SURFACES_MODEL_H_
#define APPTRAVERSE_SURFACES_MODEL_H_

#include <cstdint>
#include <stdexcept>
#include <vector>

#include "aether-objects/obj/obj.h"

#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/presenter.h"

#include "surfaces_ids.h"

namespace apptraverse {

class Surfaces;
class Surface;
class SurfacePresenter;
class AddSurfaceEvent;
class RemoveSurfaceEvent;
class SurfaceBoundsChangedEvent;

// Cascade initial outer-frame placement for a Surface before InitializeRuntimeNode
// / FinalizeDistilledGraph. desktop_* are top-left outer frame + outer size.
inline void AssignInitialDesktopBounds(Surface& surface);

// Dynamic child Surface is intentionally a Node (unlike Item). Topology of the
// live list lives on Surfaces and changes only through Events.
class Surface : public NodeFor<Surface> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::surfaces::Surface", Surface,
                           Node, 1)

 protected:
  Surface() = default;

 public:
  explicit Surface(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(number), AE_MMBR(desktop_x), AE_MMBR(desktop_y),
                    AE_MMBR(desktop_width), AE_MMBR(desktop_height),
                    AE_MMBR(surfaces), AE_MMBR(presenter))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv&) {
    throw std::runtime_error(
        "Surface v0 (pre-geometry) is not supported; start with a fresh "
        "state dir");
  }

  template <typename Dnv>
  void Load(ae::Version<1>, Dnv& dnv) {
    Node::Load(ae::Version<2>{}, dnv);
    dnv(number, desktop_x, desktop_y, desktop_width, desktop_height, surfaces,
        presenter);
  }

  template <typename Dnv>
  void Save(ae::Version<1>, Dnv& dnv) const {
    Node::Save(ae::Version<2>{}, dnv);
    dnv(number, desktop_x, desktop_y, desktop_width, desktop_height, surfaces,
        presenter);
  }

  // Model-thread: create sibling Surface + AddSurfaceEvent on parent Surfaces.
  void AddSurface();
  // Model-thread: RemoveSurfaceEvent when still live; stale double-remove no-op.
  void Remove();
  // Model-thread: SurfaceBoundsChangedEvent when values differ; equal → no-op.
  void SetDesktopBounds(std::int32_t x, std::int32_t y, std::int32_t width,
                        std::int32_t height);

  void Apply(SurfaceBoundsChangedEvent const& event);

  std::uint32_t number{0};
  // Desktop outer-frame placement (screen top-left + outer size). Mobile ignores.
  std::int32_t desktop_x{0};
  std::int32_t desktop_y{0};
  std::int32_t desktop_width{0};
  std::int32_t desktop_height{0};
  ae::ObjPtr<Surfaces> surfaces;
  ae::ObjPtr<SurfacePresenter> presenter;
};

class SurfacePresenter : public Presenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::surfaces::SurfacePresenter",
                           SurfacePresenter, Presenter, 0)

 protected:
  SurfacePresenter() = default;

 public:
  explicit SurfacePresenter(ae::ObjProp prop) : Presenter{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(surface))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, surface);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, surface);
  }

  // GUI-thread: proxy to model Surface with the same ObjId.
  void AddClick();
  void RemoveClick();

  Surface::ptr surface;
};

class Surfaces : public NodeFor<Surfaces> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::surfaces::Surfaces", Surfaces,
                           Node, 0)

 protected:
  Surfaces() = default;

 public:
  explicit Surfaces(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(surfaces))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    Node::Load(ae::Version<2>{}, dnv);
    dnv(surfaces);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    Node::Save(ae::Version<2>{}, dnv);
    dnv(surfaces);
  }

  std::vector<Surface::ptr> surfaces;

  void Apply(AddSurfaceEvent const& event);
  void Apply(RemoveSurfaceEvent const& event);
};

class AddSurfaceEvent : public EventFor<Surfaces, AddSurfaceEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::surfaces::AddSurfaceEvent",
                           AddSurfaceEvent, Event, 0)

 protected:
  AddSurfaceEvent() = default;

 public:
  explicit AddSurfaceEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(surface))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, surface);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, surface);
  }

  // Event carries the Surface object. Apply must not Create a new Surface.
  Surface::ptr surface;
};

class RemoveSurfaceEvent : public EventFor<Surfaces, RemoveSurfaceEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::surfaces::RemoveSurfaceEvent",
                           RemoveSurfaceEvent, Event, 0)

 protected:
  RemoveSurfaceEvent() = default;

 public:
  explicit RemoveSurfaceEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(surface))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, surface);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, surface);
  }

  Surface::ptr surface;
};

class SurfaceBoundsChangedEvent
    : public EventFor<Surface, SurfaceBoundsChangedEvent> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::SurfaceBoundsChangedEvent",
      SurfaceBoundsChangedEvent, Event, 0)

 protected:
  SurfaceBoundsChangedEvent() = default;

 public:
  explicit SurfaceBoundsChangedEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(x), AE_MMBR(y), AE_MMBR(width), AE_MMBR(height))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, x, y, width, height);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, x, y, width, height);
  }

  std::int32_t x{0};
  std::int32_t y{0};
  std::int32_t width{0};
  std::int32_t height{0};
};

class Application : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::surfaces::Application",
                           Application, ae::Obj, 0)

 protected:
  Application() = default;

 public:
  explicit Application(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(surfaces))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, surfaces);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, surfaces);
  }

  Surfaces::ptr surfaces;
};

inline void AssignInitialDesktopBounds(Surface& surface) {
  constexpr std::int32_t kWidth = 360;
  constexpr std::int32_t kHeight = 240;
  constexpr std::int32_t kBaseX = 120;
  constexpr std::int32_t kBaseY = 120;
  constexpr std::int32_t kCascade = 36;
  std::int32_t const offset =
      static_cast<std::int32_t>((surface.number > 0 ? surface.number - 1 : 0) *
                                kCascade);
  surface.desktop_x = kBaseX + offset;
  surface.desktop_y = kBaseY + offset;
  surface.desktop_width = kWidth;
  surface.desktop_height = kHeight;
}

void EnsureSurfacesModelRegistration();

}  // namespace apptraverse

#endif  // APPTRAVERSE_SURFACES_MODEL_H_
