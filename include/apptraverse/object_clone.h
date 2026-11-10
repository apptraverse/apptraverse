#ifndef APPTRAVERSE_OBJECT_CLONE_H_
#define APPTRAVERSE_OBJECT_CLONE_H_

#include <optional>
#include <set>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/idomain_storage.h"
#include "aether-objects/obj/obj.h"
#include "aether-objects/obj/obj_id.h"
#include "aether-objects/obj/obj_ptr.h"

namespace apptraverse {

enum class CloneError {
  kNone = 0,
  kMissingStableOrigin,
  kObjIdConflict,
  kUnsupportedGraph,
  kLoadFailed,
  kPersistFailed,
};

struct CloneOptions {
  ae::ObjId stable_origin{};
  // Caller-confirmed prefab ObjIds: when a copied object has no origin on the
  // source, its clone receives this source ObjId as origin (not the clone root).
  std::set<ae::ObjId> stable_prefab_ids;
  std::set<ae::ObjId> boundary_ids{};
  std::optional<ae::ObjId> new_root_id{};
  // Disk persistence tests; false for runtime Add (child-domain live finalize).
  bool persist_layers_to_target_storage{false};
};

struct CloneCanonicalResult {
  ae::ObjPtr<ae::Obj> root{};
  CloneError error{CloneError::kNone};

  ae::ObjId new_root_id() const { return root.is_valid() ? root.id() : ae::ObjId{}; }

  explicit operator bool() const { return error == CloneError::kNone; }
};

// Copies canonical in-memory state of source_root and copy-closure (including
// unloaded ObjPtr referents discovered via DomainGraph clone reference visitor)
// into target storage under new ObjIds; boundary_ids stay shared.
// Caller must BeginSaveSession on DirectoryDomainStorage USER before call when
// persisting to disk USER.
CloneCanonicalResult CloneCanonicalRoot(ae::Domain& source, ae::Obj& source_root,
                                      ae::Domain& target,
                                      CloneOptions const& options);

// Remaps mutable copy-closure ObjIds from captured scratch, re-serializes, and
// injects layered objects into persistent storage under base_id.
template <typename ConcreteNode>
bool InjectRemappedHistoricalBaseSnapshot(
    ConcreteNode& owner, ae::ObjId base_id,
    ae::RamDomainStorage const& captured_scratch,
    ae::IDomainStorage& persistent_storage);

}  // namespace apptraverse

#include "apptraverse/object_clone_hydrate.inl"

#endif  // APPTRAVERSE_OBJECT_CLONE_H_
