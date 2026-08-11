#ifndef APPTRAVERSE_EVENT_FOR_H_
#define APPTRAVERSE_EVENT_FOR_H_

#include "apptraverse/event.h"
#include "apptraverse/shared_discovery.h"

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

  void ReflectForSharedDiscoveryImpl(
      detail::SharedDiscoveryContext& ctx) override {
    detail::ReflectObjectForSharedDiscovery(
        static_cast<ConcreteEvent&>(*this), ctx);
  }
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EVENT_FOR_H_
