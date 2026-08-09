#ifndef APPTRAVERSE_EVENT_FOR_H_
#define APPTRAVERSE_EVENT_FOR_H_

#include "apptraverse/event.h"

namespace apptraverse {

template <typename Target, typename ConcreteEvent>
class EventFor : public Event {
 protected:
  EventFor() = default;
  explicit EventFor(ae::ObjProp prop) : Event{prop} {}

 private:
  void ApplyToImpl(ae::Obj& target) const override {
    static_cast<Target&>(target).Apply(
        static_cast<ConcreteEvent const&>(*this));
  }
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EVENT_FOR_H_
