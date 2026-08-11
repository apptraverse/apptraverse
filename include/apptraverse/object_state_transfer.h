#ifndef APPTRAVERSE_OBJECT_STATE_TRANSFER_H_
#define APPTRAVERSE_OBJECT_STATE_TRANSFER_H_

#include <algorithm>
#include <cassert>
#include <type_traits>
#include <utility>
#include <vector>

#include "aether-miscpp/reflect/domain_visitor.h"
#include "aether/obj/obj_ptr.h"

#include "apptraverse/event.h"
#include "apptraverse/node.h"
#include "apptraverse/object_link.h"
#include "apptraverse/shared_discovery.h"

namespace apptraverse {
namespace detail {

struct OwnedObjectIdCollector {
  std::vector<ae::ObjId> ids;

  void Add(ae::ObjId id) {
    assert(id.IsValid());
    for (auto const& existing : ids) {
      if (existing == id) {
        return;
      }
    }
    ids.push_back(id);
  }
};

template <typename T>
void PrepareScopedTransferObject(T& object, OwnedObjectIdCollector& owned) {
  auto visitor = ae::reflect::OverrideFunc{[&](auto& value) -> bool {
    using V = std::decay_t<decltype(value)>;
    if constexpr (IsLocalPtr<V>::value) {
      value = V{};
      return false;
    } else if constexpr (IsSharedPtr<V>::value) {
      if (value.is_valid()) {
        value.Reset();
        value.SetFlags(ae::ObjFlags::kUnloaded);
      }
      return false;
    } else if constexpr (IsObjPtr<V>::value) {
      using Target = typename ObjPtrTarget<V>::type;
      if (!value.is_valid()) {
        return false;
      }
      if (!value.is_loaded()) {
        value.Load();
      }
      assert(value.is_loaded());
      owned.Add(value.id());
      if constexpr (std::is_same_v<Target, Node>) {
        value->PrepareScopedTransfer(owned);
        return false;
      } else if constexpr (std::is_same_v<Target, Event>) {
        value->PrepareScopedTransfer(owned);
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

struct SharedDependencyCollector {
  std::vector<ae::ObjId> ids;

  void Add(ae::ObjId id) {
    assert(id.IsValid());
    for (auto const& existing : ids) {
      if (existing == id) {
        return;
      }
    }
    ids.push_back(id);
  }

  void Sort() {
    std::sort(ids.begin(), ids.end(),
              [](ae::ObjId const& a, ae::ObjId const& b) { return a < b; });
  }
};

template <typename T>
void CollectSharedDependenciesObject(T& object,
                                     SharedDependencyCollector& deps) {
  auto visitor = ae::reflect::OverrideFunc{[&](auto& value) -> bool {
    using V = std::decay_t<decltype(value)>;
    if constexpr (IsLocalPtr<V>::value) {
      return false;
    } else if constexpr (IsSharedPtr<V>::value) {
      if (value.is_valid()) {
        deps.Add(value.id());
      }
      return false;
    } else if constexpr (IsObjPtr<V>::value) {
      using Target = typename ObjPtrTarget<V>::type;
      if (!value.is_valid()) {
        return false;
      }
      if (!value.is_loaded()) {
        value.Load();
      }
      assert(value.is_loaded());
      if constexpr (std::is_same_v<Target, Node>) {
        value->CollectSharedDependencies(deps);
        return false;
      } else if constexpr (std::is_same_v<Target, Event>) {
        value->CollectSharedDependencies(deps);
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

#endif  // APPTRAVERSE_OBJECT_STATE_TRANSFER_H_
