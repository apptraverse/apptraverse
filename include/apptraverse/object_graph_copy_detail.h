#ifndef APPTRAVERSE_OBJECT_GRAPH_COPY_DETAIL_H_
#define APPTRAVERSE_OBJECT_GRAPH_COPY_DETAIL_H_

#include <cassert>
#include <cstdio>
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
      if (ctx.mode == SharedCopyMode::kReferenceExistingTargets &&
          ctx.dest_for_refs != nullptr) {
        if (!value.is_loaded()) {
          value.Load();
        }
        assert(value.is_loaded());
        auto const id = value.id();
        auto const class_id = value->GetClassId();
        bool const exists_compatible =
            StorageHasClass(*ctx.dest_for_refs, id, class_id);
        if (exists_compatible) {
          value.Reset();
          value.SetFlags(ae::ObjFlags::kUnloaded);
          return false;
        }
        if (StorageHasObject(*ctx.dest_for_refs, id)) {
          // ObjId collision: dest has this id under a different class.
          // Clone to a fresh identity and materialize the clone.
          auto const old_id = id.id();
          auto cloned = value.as_obj_ptr().Clone();
          value = V{std::move(cloned)};
          auto const new_id = value.id().id();
          std::fprintf(stderr,
                       "OBJID_CLASS_CONFLICT old_id=%u new_id=%u "
                       "expected_class=%u\n",
                       static_cast<unsigned>(old_id),
                       static_cast<unsigned>(new_id),
                       static_cast<unsigned>(class_id));
          assert(value.is_loaded());
          static_assert(std::is_base_of_v<Node, typename V::element_type>,
                        "SharedPtr target must derive from Node");
          static_cast<Node&>(*value).PrepareSyncGraph(ctx);
          value.Save();
          value.Reset();
          value.SetFlags(ae::ObjFlags::kUnloaded);
          return false;
        }
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
