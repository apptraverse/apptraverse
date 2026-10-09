#ifndef APPTRAVERSE_UI_UI_DESKTOP_WINDOW_H_
#define APPTRAVERSE_UI_UI_DESKTOP_WINDOW_H_

#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"

#include "apptraverse/ui/ui_window.h"

namespace apptraverse::ui {

class SetUiDesktopWindowFrameEvent;

class UiDesktopWindow : public NodeFor<UiDesktopWindow> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::UiDesktopWindow",
                           UiDesktopWindow, Node, 0)

 protected:
  UiDesktopWindow() = default;

 public:
  explicit UiDesktopWindow(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(window), AE_MMBR(x_), AE_MMBR(y_))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, window, x_, y_);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, window, x_, y_);
  }

  ae::ObjPtr<UiWindow> window;
  std::int32_t x() const { return x_; }
  std::int32_t y() const { return y_; }
  std::int32_t client_width() const {
    return window ? window->client_width() : 0;
  }
  std::int32_t client_height() const {
    return window ? window->client_height() : 0;
  }

  void Apply(SetUiDesktopWindowFrameEvent const& event);
  void SubmitFrameFromNative(std::int32_t x, std::int32_t y, std::int32_t width,
                             std::int32_t height);

 private:
  std::int32_t x_{100};
  std::int32_t y_{100};
};

class SetUiDesktopWindowFrameEvent
    : public EventFor<UiDesktopWindow, SetUiDesktopWindowFrameEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::SetUiDesktopWindowFrameEvent",
                           SetUiDesktopWindowFrameEvent, Event, 0)

 protected:
  SetUiDesktopWindowFrameEvent() = default;

 public:
  explicit SetUiDesktopWindowFrameEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(x), AE_MMBR(y), AE_MMBR(width), AE_MMBR(height))

  std::int32_t x{0};
  std::int32_t y{0};
  std::int32_t width{0};
  std::int32_t height{0};
};

}  // namespace apptraverse::ui

#endif  // APPTRAVERSE_UI_UI_DESKTOP_WINDOW_H_
