#ifndef APPTRAVERSE_EVENT_FOR_H_
#define APPTRAVERSE_EVENT_FOR_H_

#include <cstdint>
#include <cstdlib>
#include <limits>
#include <type_traits>

#include "aether-objects/obj/idomain_storage.h"

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

template <typename T>
bool CanCompactStoredEventLayers(ae::IDomainStorage& storage,
                                 ae::ObjId object_id) {
  for (unsigned version =
           static_cast<unsigned>(T::kCurrentVersion.value) + 1U;
       version <= static_cast<unsigned>(std::numeric_limits<std::uint8_t>::max());
       ++version) {
    ae::DomainLoad const load = storage.Load(ae::DomainQuery{
        object_id, T::kClassId, static_cast<std::uint8_t>(version)});
    if (load.result == ae::DomainLoadResult::kLoaded ||
        load.result == ae::DomainLoadResult::kRemoved) {
      return false;
    }
  }
  if constexpr (std::is_same_v<T, ae::Obj>) {
    return true;
  } else {
    return CanCompactStoredEventLayers<typename T::Base>(storage, object_id);
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

  std::uint32_t TargetClassIdImpl() const override { return Target::kClassId; }

  bool CanCompactStoredState() const override {
    if (domain == nullptr) {
      std::abort();
    }
    return detail::CanCompactStoredEventLayers<ConcreteEvent>(
        domain->domain_storage(), obj_id);
  }
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EVENT_FOR_H_
