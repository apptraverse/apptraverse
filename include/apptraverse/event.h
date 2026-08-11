#ifndef APPTRAVERSE_EVENT_H_
#define APPTRAVERSE_EVENT_H_

#include <cassert>

#include "aether/obj/obj.h"

#include "apptraverse/object_macros.h"

namespace apptraverse {

class Node;

namespace detail {
struct SharedDiscoveryContext;
struct OwnedObjectIdCollector;
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

  void PrepareScopedTransfer(detail::OwnedObjectIdCollector& owned) {
    PrepareScopedTransferImpl(owned);
  }

 private:
  void ApplyTo(ae::Obj& target) const { ApplyToImpl(target); }

  virtual void ApplyToImpl(ae::Obj& target) const = 0;

  virtual void ReflectForSharedDiscoveryImpl(
      detail::SharedDiscoveryContext& ctx) {
    (void)ctx;
    assert(false && "Concrete Event must inherit through EventFor");
  }

  virtual void PrepareScopedTransferImpl(
      detail::OwnedObjectIdCollector& owned) {
    (void)owned;
    assert(false && "Concrete Event must inherit through EventFor");
  }
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EVENT_H_
