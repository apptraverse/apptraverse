#ifndef APPTRAVERSE_MODEL_PERSISTENCE_H_
#define APPTRAVERSE_MODEL_PERSISTENCE_H_

#include <cstdint>
#include <unordered_set>
#include <vector>

#include "aether-objects/obj/obj_id.h"
#include "aether-objects/obj/obj_ptr_base.h"

#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/idomain_storage.h"
#include "aether-objects/obj/obj.h"

namespace apptraverse {

// Objects registered here participate in the live model and UI publication,
// but are omitted from persistent snapshots (RAM or directory).
void RegisterRuntimeOnlyClassId(std::uint32_t class_id);
bool IsRuntimeOnlyClassId(std::uint32_t class_id);

// Presenters and runtime-only classes are excluded from durable checkpoints.
bool IsExcludedFromPersistentSnapshot(ae::Obj const& object);

bool ObjPtrPointsToExcludedTarget(ae::ObjectPtrBase const& ref);
bool ObjPtrReferenceIsStale(
    ae::ObjectPtrBase const& ref,
    std::unordered_set<std::uint32_t> const* loaded_object_ids);

class Node;

struct PersistentObjPtrPatch {
  ae::ObjectPtrBase* slot;
  ae::ObjectPtrBase previous;
};

class PersistentSnapshotPatchSession {
 public:
  void SetLoadedObjectIds(std::unordered_set<std::uint32_t> const* ids);
  void ClearExcludedObjPtr(ae::ObjectPtrBase& ref);
  void ClearStaleObjPtr(ae::ObjectPtrBase& ref);
  void RecordObjPtrPatch(ae::ObjectPtrBase& ref);
  void RestoreAll();

  std::unordered_set<std::uint32_t> const* loaded_object_ids() const {
    return loaded_object_ids_;
  }

 private:
  std::vector<PersistentObjPtrPatch> patches_;
  std::unordered_set<std::uint32_t> const* loaded_object_ids_{nullptr};
};

using PersistentObjPtrClearHook =
    void (*)(Node& node, PersistentSnapshotPatchSession& session);
using PersistentObjPtrClearObjHook =
    void (*)(ae::Obj& object, PersistentSnapshotPatchSession& session);
void RegisterPersistentObjPtrClearHook(std::uint32_t class_id,
                                       PersistentObjPtrClearHook hook);
void RegisterPersistentObjPtrSanitizeHook(std::uint32_t class_id,
                                          PersistentObjPtrClearHook hook);
void RegisterPersistentObjPtrClearObjHook(std::uint32_t class_id,
                                          PersistentObjPtrClearObjHook hook);
void RegisterPersistentObjPtrSanitizeObjHook(std::uint32_t class_id,
                                             PersistentObjPtrClearObjHook hook);
void RunPersistentObjPtrClearHooks(ae::Obj& root,
                                   PersistentSnapshotPatchSession& session);
void SanitizeLoadedPersistentObjPtrs(
    ae::Obj& root, std::vector<ae::ObjId> const& loaded_object_ids);

// SerializePersistentModelSnapshot / LoadPersistentModelSnapshot /
// SavePersistentModelSnapshotToStorage are declared in object_serialization.h.

}  // namespace apptraverse

#endif  // APPTRAVERSE_MODEL_PERSISTENCE_H_
