#ifndef APPTRAVERSE_EVENT_H_
#define APPTRAVERSE_EVENT_H_

#include <cassert>

#include "aether/obj/idomain_storage.h"
#include "aether/obj/obj.h"

#include "apptraverse/object_graph_copy.h"
#include "apptraverse/object_macros.h"

namespace apptraverse {

class Node;

namespace detail {
struct SharedDiscoveryContext;
}

class Event : public ae::Obj {
  APPTRAVERSE_OBJECT(Event, ae::Obj, 0)

  friend class Node;

 protected:
  Event() = default;

 public:
  explicit Event(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT()

  void ReflectForSharedDiscovery(detail::SharedDiscoveryContext& ctx) {
    ReflectForSharedDiscoveryImpl(ctx);
  }

  void PrepareSyncGraph(ae::IDomainStorage* dest_for_refs,
                        SharedCopyMode mode) {
    detail::PrepareSyncGraphContext ctx{dest_for_refs, mode, {}};
    PrepareSyncGraph(ctx);
  }

  void PrepareSyncGraph(detail::PrepareSyncGraphContext& ctx) {
    PrepareSyncGraphImpl(ctx);
  }

  bool CanApplyTo(Node const& target) const {
    return CanApplyToImpl(target);
  }

 private:
  void ApplyTo(ae::Obj& target) const { ApplyToImpl(target); }

  virtual void ApplyToImpl(ae::Obj& target) const = 0;

  virtual void ReflectForSharedDiscoveryImpl(
      detail::SharedDiscoveryContext& ctx) {
    (void)ctx;
    assert(false && "Concrete Event must inherit through EventFor");
  }

  virtual void PrepareSyncGraphImpl(detail::PrepareSyncGraphContext& ctx) {
    (void)ctx;
    assert(false && "Concrete Event must inherit through EventFor");
  }

  virtual bool CanApplyToImpl(Node const& target) const {
    (void)target;
    assert(false && "Concrete Event must inherit through EventFor");
    return false;
  }
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EVENT_H_
