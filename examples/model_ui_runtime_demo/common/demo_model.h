#ifndef APPTRAVERSE_DEMO_MODEL_H_
#define APPTRAVERSE_DEMO_MODEL_H_

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "aether-objects/obj/obj.h"

#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"

#include "demo_ids.h"

namespace apptraverse {

class WindowBoundsChangedEvent;
class ColorChangedEvent;
class TextReplacedEvent;
class CenterStripAddedEvent;
class CenterStripRemovedEvent;

class ImmutableString : public ae::Obj {
  APPTRAVERSE_OBJECT(ImmutableString, ae::Obj, 0)

 protected:
  ImmutableString() = default;

 public:
  explicit ImmutableString(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(bytes))

  std::string bytes;
};

class Window;
class TextToolbar;
class ColorToolbar;
class CenterStrip;
class PaintWindow;
class LayoutWindow;

class TextToolbar : public NodeFor<TextToolbar> {
  APPTRAVERSE_OBJECT(TextToolbar, Node, 0)

 protected:
  TextToolbar() = default;

 public:
  explicit TextToolbar(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(height), AE_MMBR(text))

  std::int32_t height{demo::kTextToolbarHeight};
  ImmutableString::ptr text;

  void Apply(TextReplacedEvent const& event);
};

class ColorToolbar : public NodeFor<ColorToolbar> {
  APPTRAVERSE_OBJECT(ColorToolbar, Node, 0)

 protected:
  ColorToolbar() = default;

 public:
  explicit ColorToolbar(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(height), AE_MMBR(color), AE_MMBR(opacity),
                    AE_MMBR(arbitrary_value))

  std::int32_t height{demo::kColorToolbarHeight};
  std::uint32_t color{0x00C04040};
  std::int32_t opacity{255};
  std::uint32_t arbitrary_value{0};

  void Apply(ColorChangedEvent const& event);
  void Update(std::chrono::steady_clock::time_point now) override;

 private:
  std::chrono::steady_clock::time_point last_color_tick_{};
};

class CenterStrip : public NodeFor<CenterStrip> {
  APPTRAVERSE_OBJECT(CenterStrip, Node, 0)

 protected:
  CenterStrip() = default;

 public:
  explicit CenterStrip(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(width_numerator), AE_MMBR(width_denominator),
                    AE_MMBR(fill_color))

  std::uint32_t width_numerator{2};
  std::uint32_t width_denominator{3};
  std::uint32_t fill_color{demo::kCenterStripFill};
};

class Window : public NodeFor<Window> {
  APPTRAVERSE_OBJECT(Window, Node, 0)

 protected:
  Window() = default;

 public:
  explicit Window(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(left), AE_MMBR(top), AE_MMBR(right),
                    AE_MMBR(bottom), AE_MMBR(client_width),
                    AE_MMBR(client_height))

  std::int32_t left{0};
  std::int32_t top{0};
  std::int32_t right{0};
  std::int32_t bottom{0};
  std::int32_t client_width{0};
  std::int32_t client_height{0};

  void Apply(WindowBoundsChangedEvent const& event);
};

class PaintWindow : public NodeFor<PaintWindow, Window> {
  APPTRAVERSE_OBJECT(PaintWindow, Window, 0)

 protected:
  PaintWindow() = default;

 public:
  explicit PaintWindow(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT()
};

class LayoutWindow : public NodeFor<LayoutWindow, Window> {
  APPTRAVERSE_OBJECT(LayoutWindow, Window, 0)

 protected:
  LayoutWindow() = default;

 public:
  explicit LayoutWindow(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(text_toolbar), AE_MMBR(color_toolbar),
                    AE_MMBR(center_strips))

  TextToolbar::ptr text_toolbar;
  ColorToolbar::ptr color_toolbar;
  std::vector<CenterStrip::ptr> center_strips;

  void Apply(CenterStripAddedEvent const& event);
  void Apply(CenterStripRemovedEvent const& event);
};

class Application : public ae::Obj {
  APPTRAVERSE_OBJECT(Application, ae::Obj, 0)

 protected:
  Application() = default;

 public:
  explicit Application(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(window_a), AE_MMBR(window_b))

  PaintWindow::ptr window_a;
  LayoutWindow::ptr window_b;
};

void EnsureDemoRegistration();

}  // namespace apptraverse

#endif  // APPTRAVERSE_DEMO_MODEL_H_
