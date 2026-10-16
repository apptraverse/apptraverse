#include "apptraverse/model_persistence.h"

#include <unordered_set>
#include <vector>

#include "aether-objects/obj/obj_ptr_base.h"
#include "aether-objects/obj/registry.h"

#include "apptraverse/graph_walk.h"
#include "apptraverse/node.h"
#include "apptraverse/object_persistence_macros.h"

namespace apptraverse {
namespace {

void SanitizePersistentObjPtrFields(Node& node,
                                    PersistentSnapshotPatchSession& session) {
  ForEachReflectedObjPtrOn(node, [&](auto& field) {
    ae::ObjectPtrBase& ref = field;
    if (!ObjPtrReferenceIsStale(ref, session.loaded_object_ids())) {
      return;
    }
    session.RecordObjPtrPatch(ref);
    field = std::decay_t<decltype(field)>{};
  });
}

void SanitizePersistentObjPtrFieldsOnObject(
    ae::Obj& object, PersistentSnapshotPatchSession& session) {
  if (ae::Registry::GetRegistry().GenerationDistance(Node::kClassId,
                                                     object.GetClassId()) >= 0) {
    SanitizePersistentObjPtrFields(static_cast<Node&>(object), session);
    return;
  }
  ForEachReflectedObjPtrOn(object, [&](auto& field) {
    ae::ObjectPtrBase& ref = field;
    if (!ObjPtrReferenceIsStale(ref, session.loaded_object_ids())) {
      return;
    }
    session.RecordObjPtrPatch(ref);
    field = std::decay_t<decltype(field)>{};
  });
}

void CollectLoadedPersistentObjectsForSanitize(
    ae::Obj& root, std::vector<ae::ObjId> const& loaded_object_ids,
    std::vector<ae::Obj*>& out) {
  std::unordered_set<std::uint32_t> queued;
  std::vector<ae::ObjId> stack;
  auto queue_id = [&](ae::ObjId const& id) {
    if (!id.is_valid()) {
      return;
    }
    if (!queued.insert(id.id()).second) {
      return;
    }
    stack.push_back(id);
  };
  queue_id(root.obj_id);
  for (ae::ObjId const id : loaded_object_ids) {
    queue_id(id);
  }
  out.clear();
  while (!stack.empty()) {
    ae::ObjId const id = stack.back();
    stack.pop_back();
    auto obj = root.domain->Find(id);
    if (!obj) {
      continue;
    }
    out.push_back(&*obj);
    if (ae::Registry::GetRegistry().GenerationDistance(Node::kClassId,
                                                       obj->GetClassId()) >= 0) {
      Node& node = static_cast<Node&>(*obj);
      if (node.base.is_valid()) {
        queue_id(node.base.id());
      }
      for (EventRecord const& record : node.journal) {
        if (record.event.is_valid()) {
          queue_id(record.event.id());
        }
      }
    }
  }
}

}  // namespace

bool ObjPtrReferenceIsStale(
    ae::ObjectPtrBase const& ref,
    std::unordered_set<std::uint32_t> const* loaded_object_ids) {
  if (!ref.is_valid()) {
    return false;
  }
  if (loaded_object_ids != nullptr &&
      loaded_object_ids->count(ref.id().id()) == 0) {
    return true;
  }
  ae::Domain* const domain = ref.domain();
  if (domain == nullptr) {
    return true;
  }
  return !domain->Find(ref.id());
}

void PersistentSnapshotPatchSession::SetLoadedObjectIds(
    std::unordered_set<std::uint32_t> const* ids) {
  loaded_object_ids_ = ids;
}

void PersistentSnapshotPatchSession::ClearStaleObjPtr(
    ae::ObjectPtrBase& ref) {
  if (!ObjPtrReferenceIsStale(ref, loaded_object_ids_)) {
    return;
  }
  RecordObjPtrPatch(ref);
  ref = ae::ObjectPtrBase{};
}

void PersistentSnapshotPatchSession::RecordObjPtrPatch(
    ae::ObjectPtrBase& ref) {
  patches_.push_back(PersistentObjPtrPatch{&ref, ref});
}

void PersistentSnapshotPatchSession::RestoreAll() {
  if (restored_) {
    return;
  }
  for (PersistentObjPtrPatch& patch : patches_) {
    *patch.slot = patch.previous;
  }
  patches_.clear();
  restored_ = true;
}

void SanitizeLoadedPersistentObjPtrs(
    ae::Obj& root, std::vector<ae::ObjId> const& loaded_object_ids) {
  std::unordered_set<std::uint32_t> loaded_ids;
  loaded_ids.insert(root.obj_id.id());
  for (ae::ObjId const id : loaded_object_ids) {
    loaded_ids.insert(id.id());
  }
  PersistentSnapshotPatchSession session;
  session.SetLoadedObjectIds(&loaded_ids);
  std::vector<ae::Obj*> objects;
  CollectLoadedPersistentObjectsForSanitize(root, loaded_object_ids, objects);
  for (ae::Obj* object : objects) {
    SanitizePersistentObjPtrFieldsOnObject(*object, session);
  }
}

}  // namespace apptraverse
