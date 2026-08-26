#ifndef APPTRAVERSE_EVENT_FOR_H_
#define APPTRAVERSE_EVENT_FOR_H_

#include <type_traits>

#include "apptraverse/event.h"
#include "apptraverse/node.h"

namespace apptraverse {
namespace detail {

template <typename Target, typename ConcreteEvent>
constexpr bool HasCanApplyMethod =
    requires(Target const& target, ConcreteEvent const& event) {
      { target.CanApply(event) } -> std::convertible_to<bool>;
    };

template <typename Target, typename ConcreteEvent>
bool CanApplyEventTo(Target const& target, ConcreteEvent const& event) {
  if constexpr (HasCanApplyMethod<Target, ConcreteEvent>) {
    return target.CanApply(event);
  } else {
    return true;
  }
}

}  // namespace detail

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

  bool CanApplyToImpl(Node const& target) const override {
    return detail::CanApplyEventTo(
        static_cast<Target const&>(target),
        static_cast<ConcreteEvent const&>(*this));
  }
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EVENT_FOR_H_
