#ifndef APPTRAVERSE_EVENT_FOR_H_
#define APPTRAVERSE_EVENT_FOR_H_

#include "aether/obj/registry.h"

#include "apptraverse/event.h"
#include "apptraverse/node.h"

namespace apptraverse {

template <typename Target, typename DerivedEvent>
class EventFor : public Event {
 protected:
  EventFor() = default;
  explicit EventFor(ae::ObjProp prop) : Event{prop} {}

 private:
  bool ApplyTo(Node& target) const override {
    auto const distance = ae::Registry::GetRegistry().GenerationDistance(
        Target::kClassId, target.GetClassId());
    if (distance < 0) {
      return false;
    }

    auto& typed_target = static_cast<Target&>(target);
    typed_target.Apply(static_cast<DerivedEvent const&>(*this));
    return true;
  }
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EVENT_FOR_H_
