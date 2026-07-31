#ifndef APPTRAVERSE_EVENT_H_
#define APPTRAVERSE_EVENT_H_

#include <vector>

#include "aether/obj/obj.h"
#include "aether/obj/obj_id.h"

namespace apptraverse {

class Node;
class ReplicationState;

namespace detail {
class ObjectGraphTraversal;
struct PtrRestore;
}  // namespace detail

class Event : public ae::Obj {
  AE_OBJECT(Event, Obj, 0)

  friend class Node;
  friend class detail::ObjectGraphTraversal;

 protected:
  Event() = default;

 public:
  explicit Event(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT()

  void UnloadKnownSharedReferences(std::vector<ae::ObjId> const& known,
                                   std::vector<detail::PtrRestore>& restores) {
    UnloadKnownSharedReferencesImpl(known, restores);
  }

  void RegisterIntroducedShared(ReplicationState& state) {
    RegisterIntroducedSharedImpl(state);
  }

 private:
  virtual void ApplyTo(Node& target) const = 0;
  virtual void TraverseObjectGraph(detail::ObjectGraphTraversal& traversal) = 0;
  virtual void UnloadKnownSharedReferencesImpl(
      std::vector<ae::ObjId> const& known,
      std::vector<detail::PtrRestore>& restores) = 0;
  virtual void RegisterIntroducedSharedImpl(ReplicationState& state) = 0;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EVENT_H_
