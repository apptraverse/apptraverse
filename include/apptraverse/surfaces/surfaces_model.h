#ifndef APPTRAVERSE_SURFACES_MODEL_H_
#define APPTRAVERSE_SURFACES_MODEL_H_

#include <cstdint>
#include <vector>

#include "aether-miscpp/reflect/reflect.h"
#include "aether-objects/obj/obj.h"

#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/presenter.h"

#include "apptraverse/ui/ui_localization_string.h"
#include "apptraverse/ui/ui_locale_settings.h"

#include "apptraverse/surfaces/surfaces_ids.h"

namespace apptraverse {

class Surfaces;
class Surface;
class SurfacePresenter;
class SurfaceWindowPresenter;
class SurfaceButtonPresenter;
class AddSurfaceEvent;
class RemoveSurfaceEvent;
class SetCurrentSurfaceEvent;
class SetDesktopZOrderEvent;
class SurfaceBoundsChangedEvent;
class SurfacePresentationSizeChangedEvent;

struct SurfaceChromePresenters {
  ae::ObjPtr<SurfacePresenter> window;
  ae::ObjPtr<Presenter> add_button;
  ae::ObjPtr<Presenter> close_button;
  ae::ObjPtr<Presenter> language_button;

  AE_REFLECT_MEMBERS(window, add_button, close_button, language_button)
};

// Cascade initial outer-frame placement for a Surface before InitializeRuntimeNode
// / FinalizeDistilledGraph. desktop_* are top-left outer frame + outer size.
// Also seeds presentation_* until the native host reports the real content size.
inline void AssignInitialDesktopBounds(Surface& surface);

// Dynamic child Surface is intentionally a Node (unlike Item). Topology of the
// live list lives on Surfaces and changes only through Events.
class Surface : public NodeFor<Surface> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::surfaces::Surface", Surface,
                           Node, 0)

 protected:
  Surface() = default;

 public:
  explicit Surface(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(number), AE_MMBR(desktop_x), AE_MMBR(desktop_y),
                    AE_MMBR(desktop_width), AE_MMBR(desktop_height),
                    AE_MMBR(presentation_width), AE_MMBR(presentation_height),
                    AE_MMBR(surfaces), AE_MMBR(chrome))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, number, desktop_x, desktop_y, desktop_width, desktop_height,
        presentation_width, presentation_height, surfaces, chrome.window,
        chrome.add_button, chrome.close_button, chrome.language_button);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, number, desktop_x, desktop_y, desktop_width, desktop_height,
        presentation_width, presentation_height, surfaces, chrome.window,
        chrome.add_button, chrome.close_button, chrome.language_button);
  }

  // Model-thread: create sibling Surface + AddSurfaceEvent on parent Surfaces.
  void AddSurface();
  // Model-thread: RemoveSurfaceEvent when still live; stale double-remove no-op.
  void Remove();
  // Model-thread: SetCurrentSurfaceEvent on parent Surfaces when this Surface
  // is not already the current mobile page. Stale Surface → no-op.
  void MakeCurrent();
  // Model-thread: SurfaceBoundsChangedEvent when values differ; equal → no-op.
  void SetDesktopBounds(std::int32_t x, std::int32_t y, std::int32_t width,
                        std::int32_t height);
  // Model-thread: SurfacePresentationSizeChangedEvent when values differ.
  void SetPresentationSize(std::int32_t width, std::int32_t height);

  void Apply(SurfaceBoundsChangedEvent const& event);
  void Apply(SurfacePresentationSizeChangedEvent const& event);

  std::uint32_t number{0};
  // Desktop outer-frame placement (screen top-left + outer size). Mobile ignores.
  std::int32_t desktop_x{0};
  std::int32_t desktop_y{0};
  std::int32_t desktop_width{0};
  std::int32_t desktop_height{0};
  // Native presentation/content area used for derived UI decisions (e.g. IsWide).
  // Not desktop placement. Mobile and desktop hosts both report this.
  std::int32_t presentation_width{0};
  std::int32_t presentation_height{0};
  ae::ObjPtr<Surfaces> surfaces;
  SurfaceChromePresenters chrome;
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
  // GUI-thread: this Surface became the current mobile page.
  void PageShown();
  // GUI-thread: report native presentation/content size to the model. Does not
  // mutate the GUI-mirror Surface; orientation updates arrive via publication.
  void PresentationSizeChanged(std::int32_t width, std::int32_t height);

  // Derived from the GUI-mirror Surface after publication.
  [[nodiscard]] bool IsWide() const {
    return surface->presentation_width >= surface->presentation_height;
  }

  Surface::ptr surface;
};

class Surfaces : public NodeFor<Surfaces> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::surfaces::Surfaces", Surfaces,
                           Node, 1)

 protected:
  Surfaces() = default;

 public:
  explicit Surfaces(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(surfaces), AE_MMBR(mobile_current),
                    AE_MMBR(desktop_z_order))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, surfaces, mobile_current);
    desktop_z_order.clear();
  }

  template <typename Dnv>
  void Load(ae::Version<1>, Dnv& dnv) {
    dnv(base_, surfaces, mobile_current, desktop_z_order);
  }

  template <typename Dnv>
  void Save(ae::Version<1>, Dnv& dnv) const {
    dnv(base_, surfaces, mobile_current, desktop_z_order);
  }

  std::vector<Surface::ptr> surfaces;
  // Current mobile pager page. Empty until a mobile host reports one, and
  // after the current page was removed. Desktop hosts show every Surface at
  // once and leave it empty, the same way mobile ignores desktop_*.
  Surface::ptr mobile_current;
  // Desktop-only persisted Z-order: top → bottom among live Surface windows.
  // Empty on legacy profiles and non-desktop hosts.
  std::vector<Surface::ptr> desktop_z_order;

  void Apply(AddSurfaceEvent const& event);
  void Apply(RemoveSurfaceEvent const& event);
  void Apply(SetCurrentSurfaceEvent const& event);
  void Apply(SetDesktopZOrderEvent const& event);
  // Model-thread: authoritative snapshot from UI (ObjIds top → bottom).
  void SetDesktopZOrderFromObjIds(std::vector<std::uint32_t> ordered_obj_ids);
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

class SetCurrentSurfaceEvent
    : public EventFor<Surfaces, SetCurrentSurfaceEvent> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::SetCurrentSurfaceEvent",
      SetCurrentSurfaceEvent, Event, 0)

 protected:
  SetCurrentSurfaceEvent() = default;

 public:
  explicit SetCurrentSurfaceEvent(ae::ObjProp prop) : EventFor{prop} {}

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

class SetDesktopZOrderEvent
    : public EventFor<Surfaces, SetDesktopZOrderEvent> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::SetDesktopZOrderEvent",
      SetDesktopZOrderEvent, Event, 0)

 protected:
  SetDesktopZOrderEvent() = default;

 public:
  explicit SetDesktopZOrderEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(ordered))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, ordered);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, ordered);
  }

  std::vector<Surface::ptr> ordered;
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

class SurfacePresentationSizeChangedEvent
    : public EventFor<Surface, SurfacePresentationSizeChangedEvent> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::SurfacePresentationSizeChangedEvent",
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
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::surfaces::Application",
                           Application, ae::Obj, 0)

 protected:
  Application() = default;

 public:
  explicit Application(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(surfaces), AE_MMBR(locale), AE_MMBR(label_add),
                    AE_MMBR(label_close), AE_MMBR(window_title_prefix),
                    AE_MMBR(language_switch_label))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, surfaces, locale, label_add, label_close, window_title_prefix,
        language_switch_label);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, surfaces, locale, label_add, label_close, window_title_prefix,
        language_switch_label);
  }

  Surfaces::ptr surfaces;
  ae::ObjPtr<ui::UiLocaleSettings> locale;
  ae::ObjPtr<ui::UiLocalizationString> label_add;
  ae::ObjPtr<ui::UiLocalizationString> label_close;
  ae::ObjPtr<ui::UiLocalizationString> window_title_prefix;
  ae::ObjPtr<ui::UiLocalizationString> language_switch_label;
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
  surface.presentation_width = kWidth;
  surface.presentation_height = kHeight;
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_SURFACES_MODEL_H_
