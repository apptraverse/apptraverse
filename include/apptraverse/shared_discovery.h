#ifndef APPTRAVERSE_SHARED_DISCOVERY_H_
#define APPTRAVERSE_SHARED_DISCOVERY_H_

#include <cassert>
#include <type_traits>
#include <utility>
#include <vector>

#include "aether-miscpp/reflect/domain_visitor.h"
#include "aether/obj/obj_ptr.h"

#include "apptraverse/event.h"
#include "apptraverse/node.h"
#include "apptraverse/object_link.h"

namespace apptraverse {
namespace detail {

struct SharedDiscoveryContext {
  std::vector<Node::ptr> pending;
  std::vector<ae::ObjId> seen;

  void EnqueueShared(Node::ptr node) {
    assert(node.is_valid());
    for (auto const& id : seen) {
      if (id == node.id()) {
        return;
      }
    }
    seen.push_back(node.id());
    pending.push_back(std::move(node));
  }
};

template <typename T>
struct IsObjPtr : std::false_type {};

template <typename T>
struct IsObjPtr<ae::ObjPtr<T>> : std::true_type {};

template <typename T>
struct ObjPtrTarget;

template <typename T>
struct ObjPtrTarget<ae::ObjPtr<T>> {
  using type = T;
};

template <typename T>
void ReflectObjectForSharedDiscovery(T& object, SharedDiscoveryContext& ctx) {
  auto visitor = ae::reflect::OverrideFunc{[&](auto& value) -> bool {
    using V = std::decay_t<decltype(value)>;
    if constexpr (IsLocalPtr<V>::value) {
      return false;
    } else if constexpr (IsSharedPtr<V>::value) {
      if (value.is_valid()) {
        value.Load();
        assert(value.is_loaded());
        using Target = typename V::element_type;
        static_assert(std::is_base_of_v<Node, Target>,
                      "SharedPtr target must derive from Node");
        Node::ptr shared{value.as_obj_ptr()};
        ctx.EnqueueShared(std::move(shared));
      }
      return false;
    } else if constexpr (IsObjPtr<V>::value) {
      using Target = typename ObjPtrTarget<V>::type;
      if (value.is_valid() && !value.is_loaded()) {
        value.Load();
      }
      // Polymorphic Node/Event roots: re-enter concrete reflection via
      // NodeFor / EventFor. Do not treat AE_REF_BASE Node& this way.
      if constexpr (std::is_same_v<Target, Node>) {
        if (value.is_loaded()) {
          value->ReflectForSharedDiscovery(ctx);
        }
        return false;
      } else if constexpr (std::is_same_v<Target, Event>) {
        if (value.is_loaded()) {
          value->ReflectForSharedDiscovery(ctx);
        }
        return false;
      } else {
        return true;
      }
    } else {
      return true;
    }
  }};
  ae::reflect::DomainVisit(
      object,
      ae::reflect::DomainNodeVisitor<decltype(visitor),
                                     ae::reflect::VisitPolicy::kDeep>{
          std::move(visitor)});
}

}  // namespace detail
}  // namespace apptraverse

#endif  // APPTRAVERSE_SHARED_DISCOVERY_H_
