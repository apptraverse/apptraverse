#ifndef APPTRAVERSE_UI_UI_DESKTOP_WINDOW_H_
#define APPTRAVERSE_UI_UI_DESKTOP_WINDOW_H_

#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"

#include "apptraverse/ui/ui_presented_window.h"

namespace apptraverse::ui {

class SetUiDesktopWindowFrameEvent;
class ReportUiDesktopWindowFrameFromNativeEvent;
class RequestUiDesktopWindowFrameEvent;

class UiDesktopWindow : public NodeFor<UiDesktopWindow, UiPresentedWindow> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::UiDesktopWindow",
                           UiDesktopWindow, UiPresentedWindow, 0)

 protected:
  UiDesktopWindow() : frame_request_pending_publication_{false} {}

 public:
  explicit UiDesktopWindow(ae::ObjProp prop) : NodeFor{prop}, x_{100}, y_{100}, frame_request_pending_publication_{false} {}

  AE_OBJECT_REFLECT(AE_MMBR(x_), AE_MMBR(y_))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, x_, y_);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, x_, y_);
  }

  std::int32_t x() const { return x_; }
  std::int32_t y() const { return y_; }

  void Apply(SetUiDesktopWindowFrameEvent const& event);
  void Apply(ReportUiDesktopWindowFrameFromNativeEvent const& event);
  void Apply(RequestUiDesktopWindowFrameEvent const& event);

  void SubmitFrameFromNative(std::int32_t x, std::int32_t y,
                             std::int32_t client_width,
                             std::int32_t client_height);
  void RequestFrame(std::int32_t x, std::int32_t y, std::int32_t client_width,
                    std::int32_t client_height);

  bool TakeFrameRequestPendingPublication() {
    bool const pending = frame_request_pending_publication_;
    frame_request_pending_publication_ = false;
    return pending;
  }

 private:
  void ApplyFrameGeometry(std::int32_t x, std::int32_t y, std::int32_t width,
                          std::int32_t height);

  std::int32_t x_;
  std::int32_t y_;
  bool frame_request_pending_publication_;
};

class SetUiDesktopWindowFrameEvent
    : public EventFor<UiDesktopWindow, SetUiDesktopWindowFrameEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::SetUiDesktopWindowFrameEvent",
                           SetUiDesktopWindowFrameEvent, Event, 0)

 protected:
  SetUiDesktopWindowFrameEvent() = default;

 public:
  explicit SetUiDesktopWindowFrameEvent(ae::ObjProp prop) : EventFor{prop}, x{0}, y{0}, client_width{0}, client_height{0} {}

  AE_OBJECT_REFLECT(AE_MMBR(x), AE_MMBR(y), AE_MMBR(client_width),
                    AE_MMBR(client_height))

  std::int32_t x;
  std::int32_t y;
  std::int32_t client_width;
  std::int32_t client_height;
};

class ReportUiDesktopWindowFrameFromNativeEvent
    : public EventFor<UiDesktopWindow,
                       ReportUiDesktopWindowFrameFromNativeEvent> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::ui::ReportUiDesktopWindowFrameFromNativeEvent",
      ReportUiDesktopWindowFrameFromNativeEvent, Event, 0)

 protected:
  ReportUiDesktopWindowFrameFromNativeEvent() = default;

 public:
  explicit ReportUiDesktopWindowFrameFromNativeEvent(ae::ObjProp prop)
      : EventFor{prop}, x{0}, y{0}, client_width{0}, client_height{0} {}

  AE_OBJECT_REFLECT(AE_MMBR(x), AE_MMBR(y), AE_MMBR(client_width),
                    AE_MMBR(client_height))

  std::int32_t x;
  std::int32_t y;
  std::int32_t client_width;
  std::int32_t client_height;
};

class RequestUiDesktopWindowFrameEvent
    : public EventFor<UiDesktopWindow, RequestUiDesktopWindowFrameEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::RequestUiDesktopWindowFrameEvent",
                           RequestUiDesktopWindowFrameEvent, Event, 0)

 protected:
  RequestUiDesktopWindowFrameEvent() = default;

 public:
  explicit RequestUiDesktopWindowFrameEvent(ae::ObjProp prop)
      : EventFor{prop}, x{0}, y{0}, client_width{0}, client_height{0} {}

  AE_OBJECT_REFLECT(AE_MMBR(x), AE_MMBR(y), AE_MMBR(client_width),
                    AE_MMBR(client_height))

  std::int32_t x;
  std::int32_t y;
  std::int32_t client_width;
  std::int32_t client_height;
};

}  // namespace apptraverse::ui

#endif  // APPTRAVERSE_UI_UI_DESKTOP_WINDOW_H_
