#ifndef APPTRAVERSE_OBJECT_CLONE_H_
#define APPTRAVERSE_OBJECT_CLONE_H_

#include <optional>
#include <set>

#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj.h"
#include "aether-objects/obj/obj_id.h"

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
  std::set<ae::ObjId> boundary_ids{};
  std::optional<ae::ObjId> new_root_id{};
};

struct CloneCanonicalResult {
  ae::ObjId new_root_id{};
  CloneError error{CloneError::kNone};

  explicit operator bool() const { return error == CloneError::kNone; }
};

// Copies canonical in-memory state of source_root into target storage under a
// new ObjId (13.2: single-object closure, no internal ObjPtr remap).
// Caller must BeginSaveSession on DirectoryDomainStorage USER before call when
// persisting to disk USER. The cloned object is not kept alive in the Domain
// after return; use LoadRoot(new_root_id) to obtain a Ptr.
CloneCanonicalResult CloneCanonicalRoot(ae::Domain& source, ae::Obj& source_root,
                                      ae::Domain& target,
                                      CloneOptions const& options);

}  // namespace apptraverse

#endif  // APPTRAVERSE_OBJECT_CLONE_H_
