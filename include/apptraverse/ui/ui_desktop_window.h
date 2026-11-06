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
  UiDesktopWindow()
      : restore_client_width_{800},
        restore_client_height_{600},
        frame_request_pending_publication_{false} {}

 public:
  explicit UiDesktopWindow(ae::ObjProp prop)
      : NodeFor{prop},
        x_{100},
        y_{100},
        restore_client_width_{800},
        restore_client_height_{600},
        frame_request_pending_publication_{false} {}

  AE_OBJECT_REFLECT(AE_MMBR(x_), AE_MMBR(y_), AE_MMBR(restore_client_width_),
                    AE_MMBR(restore_client_height_))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, x_, y_, restore_client_width_, restore_client_height_);
    if (restore_client_width_ <= 0 || restore_client_height_ <= 0) {
      restore_client_width_ =
          client_width() > 0 ? client_width() : restore_client_width_;
      restore_client_height_ =
          client_height() > 0 ? client_height() : restore_client_height_;
    }
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, x_, y_, restore_client_width_, restore_client_height_);
  }

  std::int32_t x() const { return x_; }
  std::int32_t y() const { return y_; }
  std::int32_t restore_client_width() const { return restore_client_width_; }
  std::int32_t restore_client_height() const { return restore_client_height_; }

  void Apply(SetUiDesktopWindowFrameEvent const& event);
  void Apply(ReportUiDesktopWindowFrameFromNativeEvent const& event);
  void Apply(RequestUiDesktopWindowFrameEvent const& event);

  void SubmitFrameFromNative(std::int32_t x, std::int32_t y,
                             std::int32_t client_width,
                             std::int32_t client_height,
                             bool updates_restore_geometry = false);
  void RequestFrame(std::int32_t x, std::int32_t y, std::int32_t client_width,
                    std::int32_t client_height);

  bool TakeFrameRequestPendingPublication() {
    bool const pending = frame_request_pending_publication_;
    frame_request_pending_publication_ = false;
    return pending;
  }

 private:
  void ApplyRestoreFrameGeometry(std::int32_t x, std::int32_t y,
                                 std::int32_t width, std::int32_t height);
  void ApplyLiveClientSize(std::int32_t width, std::int32_t height);

  std::int32_t x_;
  std::int32_t y_;
  std::int32_t restore_client_width_;
  std::int32_t restore_client_height_;
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
      : EventFor{prop},
        x{0},
        y{0},
        client_width{0},
        client_height{0},
        updates_restore_geometry{false} {}

  AE_OBJECT_REFLECT(AE_MMBR(x), AE_MMBR(y), AE_MMBR(client_width),
                    AE_MMBR(client_height), AE_MMBR(updates_restore_geometry))

  std::int32_t x;
  std::int32_t y;
  std::int32_t client_width;
  std::int32_t client_height;
  bool updates_restore_geometry;
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
