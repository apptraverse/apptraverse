#include "apptraverse/model_persistence.h"

#include <type_traits>
#include <unordered_map>
#include <unordered_set>

#include "aether-objects/obj/obj_ptr_base.h"
#include "aether-objects/obj/registry.h"

#include "apptraverse/graph_walk.h"
#include "apptraverse/node.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/presenter.h"
#include "apptraverse/runtime_lifecycle.h"

namespace apptraverse {
namespace {

std::unordered_set<std::uint32_t>& RuntimeOnlyClassIds() {
  static std::unordered_set<std::uint32_t> ids;
  return ids;
}

void RegisterDefaultRuntimeOnlyClasses() {
  static bool once = false;
  if (once) {
    return;
  }
  once = true;
  RegisterRuntimeOnlyClassId(ApplicationRuntimeState::kClassId);
  RegisterRuntimeOnlyClassId(NetworkState::kClassId);
  RegisterRuntimeOnlyClassId(AetherRegistrationState::kClassId);
  RegisterRuntimeOnlyClassId(ApplicationStartedEvent::kClassId);
  RegisterRuntimeOnlyClassId(NetworkInitializingEvent::kClassId);
  RegisterRuntimeOnlyClassId(NetworkInterfaceUnavailableEvent::kClassId);
  RegisterRuntimeOnlyClassId(InternetUnavailableEvent::kClassId);
  RegisterRuntimeOnlyClassId(NetworkAvailableEvent::kClassId);
  RegisterRuntimeOnlyClassId(AetherRegistrationStartedEvent::kClassId);
  RegisterRuntimeOnlyClassId(AetherRegistrationCompletedEvent::kClassId);
}

std::unordered_map<std::uint32_t, PersistentObjPtrClearHook>&
PersistentObjPtrClearHooks() {
  static std::unordered_map<std::uint32_t, PersistentObjPtrClearHook> hooks;
  return hooks;
}

std::unordered_map<std::uint32_t, PersistentObjPtrClearHook>&
PersistentObjPtrSanitizeHooks() {
  static std::unordered_map<std::uint32_t, PersistentObjPtrClearHook> hooks;
  return hooks;
}

std::unordered_map<std::uint32_t, PersistentObjPtrClearObjHook>&
PersistentObjPtrClearObjHooks() {
  static std::unordered_map<std::uint32_t, PersistentObjPtrClearObjHook> hooks;
  return hooks;
}

std::unordered_map<std::uint32_t, PersistentObjPtrClearObjHook>&
PersistentObjPtrSanitizeObjHooks() {
  static std::unordered_map<std::uint32_t, PersistentObjPtrClearObjHook> hooks;
  return hooks;
}

}  // namespace

bool ObjPtrPointsToExcludedTarget(ae::ObjectPtrBase const& ref) {
  if (!ref.is_valid()) {
    return false;
  }
  ae::Domain* const domain = ref.domain();
  if (domain == nullptr) {
    return false;
  }
  auto target = domain->Find(ref.id());
  if (!target) {
    return false;
  }
  return IsExcludedFromPersistentSnapshot(*target);
}

void RegisterRuntimeOnlyClassId(std::uint32_t class_id) {
  RuntimeOnlyClassIds().insert(class_id);
}

bool IsRuntimeOnlyClassId(std::uint32_t class_id) {
  RegisterDefaultRuntimeOnlyClasses();
  return RuntimeOnlyClassIds().count(class_id) != 0;
}

bool IsExcludedFromPersistentSnapshot(ae::Obj const& object) {
  RegisterDefaultRuntimeOnlyClasses();
  auto const class_id = object.GetClassId();
  if (ae::Registry::GetRegistry().GenerationDistance(Presenter::kClassId,
                                                     class_id) >= 0) {
    return true;
  }
  return IsRuntimeOnlyClassId(class_id);
}

void PersistentSnapshotPatchSession::SetLoadedObjectIds(
    std::unordered_set<std::uint32_t> const* ids) {
  loaded_object_ids_ = ids;
}

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
  ae::Ptr<ae::Obj> target = domain->Find(ref.id());
  return !target || IsExcludedFromPersistentSnapshot(*target);
}

void PersistentSnapshotPatchSession::RecordObjPtrPatch(
    ae::ObjectPtrBase& ref) {
  patches_.push_back(PersistentObjPtrPatch{&ref, ref});
}

void PersistentSnapshotPatchSession::ClearExcludedObjPtr(
    ae::ObjectPtrBase& ref) {
  if (!ObjPtrPointsToExcludedTarget(ref)) {
    return;
  }
  RecordObjPtrPatch(ref);
  ref = ae::ObjectPtrBase{};
}

void PersistentSnapshotPatchSession::ClearStaleObjPtr(
    ae::ObjectPtrBase& ref) {
  if (!ObjPtrReferenceIsStale(ref, loaded_object_ids_)) {
    return;
  }
  ref = ae::ObjectPtrBase{};
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

void RegisterPersistentObjPtrClearHook(std::uint32_t class_id,
                                       PersistentObjPtrClearHook hook) {
  PersistentObjPtrClearHooks()[class_id] = hook;
}

void RegisterPersistentObjPtrSanitizeHook(std::uint32_t class_id,
                                          PersistentObjPtrClearHook hook) {
  PersistentObjPtrSanitizeHooks()[class_id] = hook;
}

void RegisterPersistentObjPtrClearObjHook(std::uint32_t class_id,
                                          PersistentObjPtrClearObjHook hook) {
  PersistentObjPtrClearObjHooks()[class_id] = hook;
}

void RegisterPersistentObjPtrSanitizeObjHook(std::uint32_t class_id,
                                             PersistentObjPtrClearObjHook hook) {
  PersistentObjPtrSanitizeObjHooks()[class_id] = hook;
}

void ClearPersistentObjPtrFields(Node& node,
                                 PersistentSnapshotPatchSession& session) {
  auto const hook = PersistentObjPtrClearHooks().find(node.GetClassId());
  if (hook != PersistentObjPtrClearHooks().end()) {
    hook->second(node, session);
    return;
  }
  ForEachReflectedObjPtrOn(node, [&](auto& field) {
    ae::ObjectPtrBase& ref = field;
    if (!ObjPtrPointsToExcludedTarget(ref)) {
      return;
    }
    session.RecordObjPtrPatch(ref);
    field = std::decay_t<decltype(field)>{};
  });
}

void SanitizePersistentObjPtrFields(Node& node,
                                    PersistentSnapshotPatchSession& session) {
  auto const hook = PersistentObjPtrSanitizeHooks().find(node.GetClassId());
  if (hook != PersistentObjPtrSanitizeHooks().end()) {
    hook->second(node, session);
    return;
  }
  ForEachReflectedObjPtrOn(node, [&](auto& field) {
    ae::ObjectPtrBase const& ref = field;
    if (!ObjPtrReferenceIsStale(ref, session.loaded_object_ids())) {
      return;
    }
    field = std::decay_t<decltype(field)>{};
  });
}

void ClearPersistentObjPtrFieldsOnObject(ae::Obj& object,
                                       PersistentSnapshotPatchSession& session) {
  if (IsExcludedFromPersistentSnapshot(object)) {
    return;
  }
  if (ae::Registry::GetRegistry().GenerationDistance(Node::kClassId,
                                                     object.GetClassId()) >= 0) {
    ClearPersistentObjPtrFields(static_cast<Node&>(object), session);
    return;
  }
  if (auto const hook = PersistentObjPtrClearObjHooks().find(object.GetClassId());
      hook != PersistentObjPtrClearObjHooks().end()) {
    hook->second(object, session);
  }
}

void SanitizePersistentObjPtrFieldsOnObject(
    ae::Obj& object, PersistentSnapshotPatchSession& session) {
  if (IsExcludedFromPersistentSnapshot(object)) {
    return;
  }
  if (ae::Registry::GetRegistry().GenerationDistance(Node::kClassId,
                                                     object.GetClassId()) >= 0) {
    SanitizePersistentObjPtrFields(static_cast<Node&>(object), session);
    return;
  }
  if (auto const hook = PersistentObjPtrSanitizeObjHooks().find(object.GetClassId());
      hook != PersistentObjPtrSanitizeObjHooks().end()) {
    hook->second(object, session);
  }
}

void RunPersistentObjPtrClearHooks(ae::Obj& root,
                                   PersistentSnapshotPatchSession& session) {
  std::vector<ae::Obj*> objects;
  CollectPersistentSerializationReachableObjects(root, objects);
  for (ae::Obj* object : objects) {
    ClearPersistentObjPtrFieldsOnObject(*object, session);
  }
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
    if (!obj || IsExcludedFromPersistentSnapshot(*obj)) {
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
