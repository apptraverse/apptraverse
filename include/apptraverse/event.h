#ifndef APPTRAVERSE_EVENT_H_
#define APPTRAVERSE_EVENT_H_

#include <cassert>
#include <cstdint>
#include <map>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/obj.h"

#include "apptraverse/object_macros.h"

namespace apptraverse {

class Node;
struct SharedEventId;
struct SharedEventOrder;

class Event : public ae::Obj {
  APPTRAVERSE_OBJECT(Event, ae::Obj, 0)

  friend class Node;

 protected:
  Event() = default;

 public:
  explicit Event(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT()

  bool CanApplyTo(Node const& target) const { return CanApplyToImpl(target); }

  virtual bool MatchesSharedMetadata(
      SharedEventId const& identity,
      SharedEventOrder const& order) const {
    (void)identity;
    (void)order;
    return true;
  }

  // Most-derived Node class this Event is written against. Used to reject an
  // untrusted Event whose EventFor target is not this Node, before CanApplyTo
  // or ApplyTo would static_cast.
  std::uint32_t TargetClassId() const { return TargetClassIdImpl(); }

 private:
  void ApplyTo(ae::Obj& target) const { ApplyToImpl(target); }

  virtual void ApplyToImpl(ae::Obj& target) const = 0;

  virtual bool CanApplyToImpl(Node const& target) const {
    (void)target;
    assert(false && "Concrete Event must inherit through EventFor");
    return false;
  }

  virtual std::uint32_t TargetClassIdImpl() const {
    assert(false && "Concrete Event must inherit through EventFor");
    return 0;
  }
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EVENT_H_
