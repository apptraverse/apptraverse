#ifndef APPTRAVERSE_UI_UI_WINDOW_H_
#define APPTRAVERSE_UI_UI_WINDOW_H_

#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"

namespace apptraverse::ui {

class SetUiWindowClientSizeEvent;

class UiWindow : public NodeFor<UiWindow> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::UiWindow", UiWindow, Node, 0)

 protected:
  UiWindow() = default;

 public:
  explicit UiWindow(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(client_width_), AE_MMBR(client_height_))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, client_width_, client_height_);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, client_width_, client_height_);
  }

  std::int32_t client_width() const { return client_width_; }
  std::int32_t client_height() const { return client_height_; }

  void Apply(SetUiWindowClientSizeEvent const& event);

 private:
  std::int32_t client_width_{800};
  std::int32_t client_height_{600};
};

class SetUiWindowClientSizeEvent
    : public EventFor<UiWindow, SetUiWindowClientSizeEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::SetUiWindowClientSizeEvent",
                           SetUiWindowClientSizeEvent, Event, 0)

 protected:
  SetUiWindowClientSizeEvent() = default;

 public:
  explicit SetUiWindowClientSizeEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(width), AE_MMBR(height))

  std::int32_t width{0};
  std::int32_t height{0};
};

}  // namespace apptraverse::ui

#endif  // APPTRAVERSE_UI_UI_WINDOW_H_
