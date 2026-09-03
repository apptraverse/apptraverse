#ifndef APPTRAVERSE_GRAPH_WALK_H_
#define APPTRAVERSE_GRAPH_WALK_H_

#include <type_traits>
#include <utility>

#include "aether-miscpp/reflect/reflect.h"
#include "aether-objects/obj/obj.h"
#include "aether-objects/obj/obj_ptr.h"

#include "apptraverse/event.h"
#include "apptraverse/node.h"

namespace apptraverse {
namespace detail {

template <typename T>
constexpr bool IsExecutionTarget =
    std::is_same_v<T, Node> || std::is_same_v<T, Event>;

template <typename T, typename Fn>
void CallIfGraphEdgeObjPtr(ae::ObjPtr<T>& pointer, Fn&& fn) {
  if constexpr (!IsExecutionTarget<T>) {
    fn(pointer);
  }
}

template <typename T, typename Fn>
void CallIfGraphEdgeObjPtr(ae::ObjPtr<T> const& pointer, Fn&& fn) {
  if constexpr (!IsExecutionTarget<T>) {
    fn(pointer);
  }
}

template <typename Fn>
void CallIfGraphEdgeObjPtr(auto&, Fn&&) {}

template <typename T, typename Fn>
  requires(ae::reflect::Reflectable<T>)
void ForEachReflectedGraphEdgeObjPtr(T& obj, Fn&& fn) {
  auto reflection = ae::reflect::make_reflection(obj);
  reflection.Apply([&](auto&&... fields) {
    (CallIfGraphEdgeObjPtr(fields, fn), ...);
  });
}

}  // namespace detail

template <typename T, typename Fn>
void ForEachGraphEdgeObjPtrOn(T& obj, Fn&& fn) {
  detail::ForEachReflectedGraphEdgeObjPtr(obj, std::forward<Fn>(fn));
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_GRAPH_WALK_H_
