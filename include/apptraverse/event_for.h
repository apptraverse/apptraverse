#ifndef APPTRAVERSE_EVENT_FOR_H_
#define APPTRAVERSE_EVENT_FOR_H_

#include <vector>

#include "aether/obj/obj_id.h"

#include "apptraverse/event.h"
#include "apptraverse/event_graph_packager.h"
#include "apptraverse/replication_state.h"

namespace apptraverse {

class Node;

template <typename Target, typename ConcreteEvent>
class EventFor : public Event {
 protected:
  EventFor() = default;
  explicit EventFor(ae::ObjProp prop) : Event{prop} {}

 private:
  void ApplyTo(Node& target) const override {
    static_cast<Target&>(target).Apply(
        static_cast<ConcreteEvent const&>(*this));
  }

  void UnloadKnownSharedReferencesImpl(
      std::vector<ae::ObjId> const& known,
      std::vector<detail::PtrRestore>& restores) override {
    detail::UnloadKnownOnConcrete(static_cast<ConcreteEvent&>(*this), known,
                                  restores);
  }

  void RegisterIntroducedSharedImpl(ReplicationState& state) override {
    detail::RegisterIntroducedOnConcrete(static_cast<ConcreteEvent&>(*this),
                                         state);
  }
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EVENT_FOR_H_
