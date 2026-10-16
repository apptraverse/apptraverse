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
  void ClearStaleObjPtr(ae::ObjectPtrBase& ref);
  void RecordObjPtrPatch(ae::ObjectPtrBase& ref);
  void RestoreAll();

  std::unordered_set<std::uint32_t> const* loaded_object_ids() const {
    return loaded_object_ids_;
  }

  bool restored() const { return restored_; }

 private:
  friend class PersistentSnapshotPatchGuard;
  std::vector<PersistentObjPtrPatch> patches_;
  std::unordered_set<std::uint32_t> const* loaded_object_ids_{nullptr};
  bool restored_{false};
};

// Restores ObjPtr patches on scope exit unless dismissed or already restored.
class PersistentSnapshotPatchGuard {
 public:
  explicit PersistentSnapshotPatchGuard(PersistentSnapshotPatchSession& session)
      : session_{session} {}
  ~PersistentSnapshotPatchGuard() { release(); }
  PersistentSnapshotPatchGuard(PersistentSnapshotPatchGuard const&) = delete;
  PersistentSnapshotPatchGuard& operator=(PersistentSnapshotPatchGuard const&) = delete;

  void dismiss() { release(); }

 private:
  void release() {
    if (!dismissed_ && !session_.restored_) {
      session_.RestoreAll();
    }
    dismissed_ = true;
  }
  PersistentSnapshotPatchSession& session_;
  bool dismissed_{false};
};

void SanitizeLoadedPersistentObjPtrs(
    ae::Obj& root, std::vector<ae::ObjId> const& loaded_object_ids);

// SerializePersistentModelSnapshot / LoadPersistentModelSnapshot /
// SavePersistentModelSnapshotToStorage are declared in object_serialization.h.

}  // namespace apptraverse

#endif  // APPTRAVERSE_MODEL_PERSISTENCE_H_
