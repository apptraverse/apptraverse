#ifndef APPTRAVERSE_OBJECT_GRAPH_COPY_DETAIL_H_
#define APPTRAVERSE_OBJECT_GRAPH_COPY_DETAIL_H_

#include <cassert>
#include <type_traits>
#include <utility>

#include "aether-miscpp/domain_visitor/domain_visitor.h"
#include "aether/obj/idomain_storage.h"
#include "aether/obj/obj_ptr.h"

#include "apptraverse/event.h"
#include "apptraverse/node.h"
#include "apptraverse/object_graph_copy.h"
#include "apptraverse/object_link.h"
#include "apptraverse/shared_discovery.h"

namespace apptraverse {
namespace detail {

template <typename T>
void PrepareSyncGraphObject(T& object, PrepareSyncGraphContext& ctx);

template <typename T>
void PrepareSyncGraphObject(T& object, PrepareSyncGraphContext& ctx) {
  auto visitor = [&](auto& value) -> bool {
    using V = std::decay_t<decltype(value)>;
    if constexpr (IsLocalPtr<V>::value) {
      value = V{};
      return false;
    } else if constexpr (IsSharedPtr<V>::value) {
      if (!value.is_valid()) {
        return false;
      }
      bool const exists =
          ctx.dest_for_refs != nullptr &&
          StorageHasObject(*ctx.dest_for_refs, value.id());
      if (ctx.mode == SharedCopyMode::kReferenceExistingTargets && exists) {
        value.Reset();
        value.SetFlags(ae::ObjFlags::kUnloaded);
        return false;
      }
      if (!value.is_loaded()) {
        value.Load();
      }
      assert(value.is_loaded());
      static_assert(std::is_base_of_v<Node, typename V::element_type>,
                    "SharedPtr target must derive from Node");
      static_cast<Node&>(*value).PrepareSyncGraph(ctx);
      if (ctx.mode == SharedCopyMode::kReferenceExistingTargets) {
        value.Save();
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
      if constexpr (std::is_base_of_v<Node, Target>) {
        static_cast<Node&>(*value).PrepareSyncGraph(ctx);
        return false;
      } else if constexpr (std::is_base_of_v<Event, Target>) {
        static_cast<Event&>(*value).PrepareSyncGraph(ctx);
        return false;
      } else {
        return true;
      }
    } else {
      return true;
    }
  };
  ae::domain_visitor::DomainVisit(
      object,
      ae::domain_visitor::DomainNodeVisitor<
          decltype(visitor), ae::domain_visitor::VisitPolicy::kDeep>{
          std::move(visitor)});
}

}  // namespace detail
}  // namespace apptraverse

#endif  // APPTRAVERSE_OBJECT_GRAPH_COPY_DETAIL_H_
