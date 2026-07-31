#ifndef APPTRAVERSE_EVENT_GRAPH_PACKAGER_H_
#define APPTRAVERSE_EVENT_GRAPH_PACKAGER_H_

#include <algorithm>
#include <cassert>
#include <type_traits>
#include <utility>
#include <vector>

#include "aether-miscpp/reflect/domain_visitor.h"
#include "aether/obj/obj.h"
#include "aether/obj/obj_id.h"
#include "aether/obj/obj_ptr.h"

#include "apptraverse/event.h"
#include "apptraverse/node.h"
#include "apptraverse/replication_state.h"

namespace apptraverse {
namespace detail {

template <typename T>
struct IsObjPtr : std::false_type {};

template <typename T>
struct IsObjPtr<ae::ObjPtr<T>> : std::true_type {};

template <typename T>
struct ObjPtrPointee;

template <typename T>
struct ObjPtrPointee<ae::ObjPtr<T>> {
  using Type = T;
};

struct PtrRestore {
  void* address{nullptr};
  void (*restore)(void*){nullptr};

  void Apply() const {
    assert(address != nullptr);
    assert(restore != nullptr);
    restore(address);
  }
};

template <typename T>
PtrRestore MakeRestore(ae::ObjPtr<T>& ptr) {
  return PtrRestore{
      &ptr,
      [](void* address) {
        auto& typed = *static_cast<ae::ObjPtr<T>*>(address);
        if (typed.is_valid() && !typed.is_loaded()) {
          typed.Load();
        }
      },
  };
}

inline bool ContainsId(std::vector<ae::ObjId> const& ids, ae::ObjId id) {
  return std::find(ids.begin(), ids.end(), id) != ids.end();
}

template <typename Concrete>
void UnloadKnownOnConcrete(Concrete& concrete,
                           std::vector<ae::ObjId> const& known,
                           std::vector<PtrRestore>& restores) {
  auto visitor = [&](auto& current) -> bool {
    using Current = std::remove_cvref_t<decltype(current)>;
    if constexpr (IsObjPtr<Current>::value) {
      if (!current.is_valid() || !current.is_loaded()) {
        return false;
      }
      if (ContainsId(known, current.id())) {
        restores.push_back(MakeRestore(current));
        current.Reset();
        current.SetFlags(ae::ObjFlags::kUnloadedByDefault);
        return false;
      }
      return true;
    }
    return true;
  };

  using DeepVisitor =
      ae::reflect::DomainNodeVisitor<decltype(visitor),
                                     ae::reflect::VisitPolicy::kDeep>;
  ae::reflect::CycleDetector detector;
  ae::reflect::DomainVisit(detector, concrete, DeepVisitor{std::move(visitor)});
}

template <typename Concrete>
void RegisterIntroducedOnConcrete(Concrete& concrete, ReplicationState& state) {
  auto visitor = [&](auto& current) -> bool {
    using Current = std::remove_cvref_t<decltype(current)>;
    if constexpr (IsObjPtr<Current>::value) {
      if (current.is_valid() && current.is_loaded()) {
        using Pointee = typename ObjPtrPointee<Current>::Type;
        if constexpr (!std::is_base_of_v<Event, Pointee>) {
          state.RegisterShared(current.id());
        }
        if constexpr (std::is_base_of_v<Node, Pointee>) {
          state.RegisterSharedNode(current.id());
        }
        return true;
      }
      return false;
    }
    if constexpr (std::is_base_of_v<Node, Current>) {
      if (current.obj_id.IsValid()) {
        state.RegisterSharedNode(current.obj_id);
      }
      return true;
    }
    if constexpr (std::is_base_of_v<ae::Obj, Current> &&
                  !std::is_base_of_v<Event, Current>) {
      if (current.obj_id.IsValid()) {
        state.RegisterShared(current.obj_id);
      }
    }
    return true;
  };

  using DeepVisitor =
      ae::reflect::DomainNodeVisitor<decltype(visitor),
                                     ae::reflect::VisitPolicy::kDeep>;
  ae::reflect::CycleDetector detector;
  ae::reflect::DomainVisit(detector, concrete, DeepVisitor{std::move(visitor)});
}

class EventGraphPackager {
 public:
  explicit EventGraphPackager(std::vector<ae::ObjId> const& known_before_event)
      : known_{&known_before_event} {}

  std::vector<PtrRestore> UnloadKnownReferences(Event& event) {
    std::vector<PtrRestore> restores;
    event.UnloadKnownSharedReferences(*known_, restores);
    return restores;
  }

  static void Restore(std::vector<PtrRestore> const& restores) {
    for (auto const& restore : restores) {
      restore.Apply();
    }
  }

  static void RegisterIntroducedObjects(Event& event, ReplicationState& state) {
    event.RegisterIntroducedShared(state);
  }

  static bool ContainsId(std::vector<ae::ObjId> const& ids, ae::ObjId id) {
    return detail::ContainsId(ids, id);
  }

 private:
  std::vector<ae::ObjId> const* known_;
};

}  // namespace detail
}  // namespace apptraverse

#endif  // APPTRAVERSE_EVENT_GRAPH_PACKAGER_H_
