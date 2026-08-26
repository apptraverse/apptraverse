#ifndef APPTRAVERSE_GRAPH_WALK_H_
#define APPTRAVERSE_GRAPH_WALK_H_

#include <type_traits>
#include <utility>

#include "aether-miscpp/domain_visitor/domain_visitor.h"
#include "aether/obj/obj.h"
#include "aether/obj/obj_ptr.h"

#include "apptraverse/event.h"
#include "apptraverse/node.h"

namespace apptraverse {
namespace detail {

template <typename T>
constexpr bool IsExecutionTarget =
    std::is_same_v<T, Node> || std::is_same_v<T, Event>;

template <typename T, typename Fn>
void CallIfObjPtr(ae::ObjPtr<T>& pointer, Fn&& fn) {
  if constexpr (!IsExecutionTarget<T>) {
    fn(pointer);
  }
}

template <typename T, typename Fn>
void CallIfObjPtr(ae::ObjPtr<T> const& pointer, Fn&& fn) {
  if constexpr (!IsExecutionTarget<T>) {
    fn(pointer);
  }
}

template <typename Fn>
void CallIfObjPtr(auto&, Fn&&) {}

}  // namespace detail

template <typename T, typename Fn>
void ForEachMaterializedPtrFieldOn(T& obj, Fn&& fn) {
  ae::domain_visitor::DomainVisit(
      obj, [&](auto& field) { detail::CallIfObjPtr(field, fn); },
      ae::domain_visitor::PolicyConst<
          ae::domain_visitor::VisitPolicy::kShallow>{});
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_GRAPH_WALK_H_
