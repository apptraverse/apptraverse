#ifndef APPTRAVERSE_MESSENGER_MODEL_H_
#define APPTRAVERSE_MESSENGER_MODEL_H_

#include <cstdint>
#include <vector>

#include "aether-objects/obj/obj.h"

#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/presenter.h"

#include "messenger_ids.h"

namespace apptraverse {

class Surfaces;
class Surface;
class SurfacePresenter;
class SurfaceBoundsChangedEvent;
class SurfacePresentationSizeChangedEvent;

// Cascade initial outer-frame placement for the single messenger Surface.
inline void AssignInitialDesktopBounds(Surface& surface);

// Single-surface host topology. List holds exactly one Surface after distill;
// multi-surface add/remove is intentionally absent from this example.
class Surface : public NodeFor<Surface> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::messenger::Surface", Surface,
                           Node, 0)

 protected:
  Surface() = default;

 public:
  explicit Surface(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(number), AE_MMBR(desktop_x), AE_MMBR(desktop_y),
                    AE_MMBR(desktop_width), AE_MMBR(desktop_height),
                    AE_MMBR(presentation_width), AE_MMBR(presentation_height),
                    AE_MMBR(surfaces), AE_MMBR(presenter))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, number, desktop_x, desktop_y, desktop_width, desktop_height,
        presentation_width, presentation_height, surfaces, presenter);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, number, desktop_x, desktop_y, desktop_width, desktop_height,
        presentation_width, presentation_height, surfaces, presenter);
  }

  // Model-thread: SurfaceBoundsChangedEvent when values differ; equal → no-op.
  void SetDesktopBounds(std::int32_t x, std::int32_t y, std::int32_t width,
                        std::int32_t height);
  // Model-thread: SurfacePresentationSizeChangedEvent when values differ.
  void SetPresentationSize(std::int32_t width, std::int32_t height);

  void Apply(SurfaceBoundsChangedEvent const& event);
  void Apply(SurfacePresentationSizeChangedEvent const& event);

  std::uint32_t number{0};
  std::int32_t desktop_x{0};
  std::int32_t desktop_y{0};
  std::int32_t desktop_width{0};
  std::int32_t desktop_height{0};
  std::int32_t presentation_width{0};
  std::int32_t presentation_height{0};
  ae::ObjPtr<Surfaces> surfaces;
  ae::ObjPtr<SurfacePresenter> presenter;
};

class SurfacePresenter : public Presenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::messenger::SurfacePresenter",
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

  // GUI-thread: report native presentation/content size to the model.
  void PresentationSizeChanged(std::int32_t width, std::int32_t height);

  Surface::ptr surface;
};

class Surfaces : public NodeFor<Surfaces> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::messenger::Surfaces", Surfaces,
                           Node, 0)

 protected:
  Surfaces() = default;

 public:
  explicit Surfaces(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(surfaces))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, surfaces);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, surfaces);
  }

  std::vector<Surface::ptr> surfaces;
};

class SurfaceBoundsChangedEvent
    : public EventFor<Surface, SurfaceBoundsChangedEvent> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::messenger::SurfaceBoundsChangedEvent",
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

class SurfacePresentationSizeChangedEvent
    : public EventFor<Surface, SurfacePresentationSizeChangedEvent> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::messenger::SurfacePresentationSizeChangedEvent",
      SurfacePresentationSizeChangedEvent, Event, 0)

 protected:
  SurfacePresentationSizeChangedEvent() = default;

 public:
  explicit SurfacePresentationSizeChangedEvent(ae::ObjProp prop)
      : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(width), AE_MMBR(height))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, width, height);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, width, height);
  }

  std::int32_t width{0};
  std::int32_t height{0};
};

class Application : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::messenger::Application",
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
  constexpr std::int32_t kWidth = 640;
  constexpr std::int32_t kHeight = 480;
  constexpr std::int32_t kBaseX = 120;
  constexpr std::int32_t kBaseY = 120;
  surface.desktop_x = kBaseX;
  surface.desktop_y = kBaseY;
  surface.desktop_width = kWidth;
  surface.desktop_height = kHeight;
  surface.presentation_width = kWidth;
  surface.presentation_height = kHeight;
}

void EnsureMessengerModelRegistration();

}  // namespace apptraverse

#endif  // APPTRAVERSE_MESSENGER_MODEL_H_
