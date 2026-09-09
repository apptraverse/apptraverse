#ifndef APPTRAVERSE_SURFACES_MODEL_H_
#define APPTRAVERSE_SURFACES_MODEL_H_

#include <cstdint>
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

// Dynamic child Surface is intentionally a Node (unlike Item). Topology of the
// live list lives on Surfaces and changes only through Events.
class Surface : public NodeFor<Surface> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::surfaces::Surface", Surface,
                           Node, 0)

 protected:
  Surface() = default;

 public:
  explicit Surface(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(number), AE_MMBR(surfaces), AE_MMBR(presenter))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    Node::Load(ae::Version<2>{}, dnv);
    dnv(number, surfaces, presenter);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    Node::Save(ae::Version<2>{}, dnv);
    dnv(number, surfaces, presenter);
  }

  // Model-thread: create sibling Surface + AddSurfaceEvent on parent Surfaces.
  void AddSurface();
  // Model-thread: RemoveSurfaceEvent when still live; stale double-remove no-op.
  void Remove();

  std::uint32_t number{0};
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

void EnsureSurfacesModelRegistration();

}  // namespace apptraverse

#endif  // APPTRAVERSE_SURFACES_MODEL_H_
