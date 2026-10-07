#ifndef APPTRAVERSE_CONTROL_BUTTON_H_
#define APPTRAVERSE_CONTROL_BUTTON_H_

#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"

namespace apptraverse {

class ControlButton;
class ControlButtonClickedEvent;

class ControlButton : public NodeFor<ControlButton> {
  APPTRAVERSE_OBJECT(ControlButton, Node, 0)

 protected:
  ControlButton() = default;

 public:
  explicit ControlButton(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(caption))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, caption);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, caption);
  }

  std::string caption;

  void RequestClick();
  void Apply(ControlButtonClickedEvent const& event);
};

class ControlButtonClickedEvent
    : public EventFor<ControlButton, ControlButtonClickedEvent> {
  APPTRAVERSE_OBJECT(ControlButtonClickedEvent, Event, 0)

 protected:
  ControlButtonClickedEvent() = default;

 public:
  explicit ControlButtonClickedEvent(ae::ObjProp prop) : EventFor{prop} {}
};

void EnsureControlButtonRegistration();

}  // namespace apptraverse

#endif  // APPTRAVERSE_CONTROL_BUTTON_H_
