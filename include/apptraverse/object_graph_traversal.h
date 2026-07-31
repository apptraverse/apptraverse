#ifndef APPTRAVERSE_OBJECT_GRAPH_TRAVERSAL_H_
#define APPTRAVERSE_OBJECT_GRAPH_TRAVERSAL_H_

#include <algorithm>
#include <type_traits>
#include <utility>
#include <vector>

#include "aether-miscpp/reflect/reflect.h"
#include "aether/obj/obj_id.h"
#include "aether/obj/obj_ptr.h"

#include "apptraverse/event.h"
#include "apptraverse/node.h"

namespace apptraverse {

template <typename Target, typename ConcreteEvent>
class EventFor;

namespace detail {

template <typename T>
struct TraversalIsObjPtr : std::false_type {};

template <typename T>
struct TraversalIsObjPtr<ae::ObjPtr<T>> : std::true_type {};

template <typename T>
struct TraversalObjPtrPointee;

template <typename T>
struct TraversalObjPtrPointee<ae::ObjPtr<T>> {
  using Type = T;
};

class ObjectGraphTraversal {
  template <typename Target, typename ConcreteEvent>
  friend class ::apptraverse::EventFor;

 public:
  virtual ~ObjectGraphTraversal() = default;

  template <typename Root>
  void Traverse(Root& root) {
    VisitValue(root);
  }

 protected:
  virtual void OnNode(Node& node) = 0;

 private:
  template <typename Value>
  void VisitValue(Value& value) {
    using ValueType = std::remove_cvref_t<Value>;

    if constexpr (std::is_base_of_v<Node, ValueType>) {
      if (!MarkNode(static_cast<Node&>(value))) {
        return;
      }
      OnNode(static_cast<Node&>(value));
    }

    if constexpr (std::is_base_of_v<Event, ValueType>) {
      if (!MarkObject(static_cast<ae::Obj&>(value))) {
        return;
      }
      static_cast<Event&>(value).TraverseObjectGraph(*this);
      return;
    }

    if constexpr (ae::reflect::IsReflectable<ValueType>::value) {
      if constexpr (std::is_base_of_v<ae::Obj, ValueType>) {
        if (!std::is_base_of_v<Node, ValueType>) {
          if (!MarkObject(static_cast<ae::Obj&>(value))) {
            return;
          }
        }
      }
      ae::reflect::Reflection{value}.Apply(
          [&](auto&... fields) { (HandleField(fields), ...); });
    }
  }

  template <typename Field>
  void HandleField(Field& field) {
    using FieldType = std::remove_cvref_t<Field>;

    if constexpr (TraversalIsObjPtr<FieldType>::value) {
      if (!field.is_valid() || !field.is_loaded()) {
        return;
      }
      using Pointee = typename TraversalObjPtrPointee<FieldType>::Type;
      VisitValue(*field);
      (void)sizeof(Pointee);
    } else if constexpr (requires { typename FieldType::value_type; }) {
      if constexpr (std::is_same_v<FieldType,
                                   std::vector<typename FieldType::value_type>>) {
        for (auto& element : field) {
          HandleField(element);
        }
      }
    }
  }

  template <typename Value>
  void TraverseConcrete(Value& value) {
    VisitValue(value);
  }

  bool MarkNode(Node& node) {
    assert(node.obj_id.IsValid());
    if (std::find(visited_nodes_.begin(), visited_nodes_.end(), node.obj_id) !=
        visited_nodes_.end()) {
      return false;
    }
    visited_nodes_.push_back(node.obj_id);
    return true;
  }

  bool MarkObject(ae::Obj& object) {
    assert(object.obj_id.IsValid());
    if (std::find(visited_objects_.begin(), visited_objects_.end(),
                  object.obj_id) != visited_objects_.end()) {
      return false;
    }
    visited_objects_.push_back(object.obj_id);
    return true;
  }

  std::vector<ae::ObjId> visited_nodes_;
  std::vector<ae::ObjId> visited_objects_;
};

}  // namespace detail
}  // namespace apptraverse

#endif  // APPTRAVERSE_OBJECT_GRAPH_TRAVERSAL_H_
