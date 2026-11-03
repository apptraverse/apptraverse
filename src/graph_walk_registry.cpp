#include "apptraverse/graph_walk.h"

#include <unordered_map>

#include "aether-objects/obj/obj_ptr_base.h"

namespace apptraverse {
namespace detail {

using EmitGraphEdgeFn = void (*)(ae::ObjectPtrBase& edge, void* ctx);
using ClassGraphEdgeWalkFn = void (*)(ae::Obj& obj, EmitGraphEdgeFn emit,
                                      void* ctx);

std::unordered_map<std::uint32_t, ClassGraphEdgeWalkFn>& ClassGraphEdgeWalks() {
  static std::unordered_map<std::uint32_t, ClassGraphEdgeWalkFn> walks;
  return walks;
}

void RegisterClassGraphEdgeWalk(std::uint32_t class_id,
                                ClassGraphEdgeWalkFn walk) {
  ClassGraphEdgeWalks().insert_or_assign(class_id, walk);
}

ClassGraphEdgeWalkFn FindClassGraphEdgeWalk(std::uint32_t class_id) {
  auto const it = ClassGraphEdgeWalks().find(class_id);
  if (it == ClassGraphEdgeWalks().end()) {
    return nullptr;
  }
  return it->second;
}

}  // namespace detail

void ForEachGraphEdgeObjPtrOnObject(
    ae::Obj& obj, void (*emit)(ae::ObjectPtrBase& edge, void* ctx),
    void* ctx) {
  detail::ClassGraphEdgeWalkFn const walk =
      detail::FindClassGraphEdgeWalk(obj.GetClassId());
  if (walk == nullptr) {
    return;
  }
  walk(obj, emit, ctx);
}

}  // namespace apptraverse
