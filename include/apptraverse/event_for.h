#ifndef APPTRAVERSE_EVENT_FOR_H_
#define APPTRAVERSE_EVENT_FOR_H_

#include "apptraverse/event.h"

namespace apptraverse {

template <typename Target, typename DerivedEvent>
class EventFor : public Event {
 protected:
  EventFor() = default;
  explicit EventFor(ae::ObjProp prop) : Event{prop} {}

 private:
  void ApplyTo(Node& target) const override {
    auto& typed_target = static_cast<Target&>(target);
    typed_target.Apply(static_cast<DerivedEvent const&>(*this));
  }
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EVENT_FOR_H_
